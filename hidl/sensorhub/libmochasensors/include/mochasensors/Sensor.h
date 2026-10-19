/*
 * Copyright (C) 2026 Artem Bambalov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MOCHA_SENSORS_SENSOR_H
#define MOCHA_SENSORS_SENSOR_H

#include <hardware/sensors.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mochasensors/Calibration.h"
#include "mochasensors/IioDevice.h"
#include "mochasensors/InputDevice.h"

namespace mocha {
namespace sensors {

/*
 * One sensor: what it is, how it is switched on, and how what its device
 * gives turns into events. The hub only holds a set of these and listens to
 * their descriptors; everything particular to a sensor lives in its class.
 */
class Sensor {
  public:
    virtual ~Sensor() = default;

    /* Whether its device was there to open. */
    bool ok() const { return ok_; }
    const sensor_t& info() const { return info_; }
    void SetHandle(int handle) { info_.handle = handle; }
    bool active() const { return active_; }
    uint64_t events() const { return events_; }

    /* The descriptors to wait on; OnReadable gets each back. */
    virtual std::vector<int> Fds() const = 0;
    /* 0 or -errno, as libhardware's sensors calls. */
    virtual int Activate(bool enabled) = 0;
    /* The period asked for, and how long samples may wait before they are
     * delivered: a sensor whose hub keeps a FIFO batches them that long. */
    virtual int Batch(int64_t periodNs, int64_t latencyNs) = 0;
    /* Nothing is held back by default, so all there is has been delivered
     * when the call comes. */
    virtual int Flush(std::vector<sensors_event_t>* out);
    /* What a sensor reports as soon as it is switched on, if anything: an
     * on-change sensor's current value. */
    virtual void OnActivated(std::vector<sensors_event_t>* /* out */) {}
    virtual void OnReadable(int fd, std::vector<sensors_event_t>* out) = 0;

    /* Whether its reading comes from the hub's nine-axis fusion, which a
     * restart of the magnetometer's calibration sets going again. */
    virtual bool UsesNineAxisFusion() const { return false; }
    /* True once, after the fusion has put out nothing but NaN for a while. */
    virtual bool TakeFusionFault() { return false; }
    /* Its device held off, or let run again, without the sensor counting as
     * switched off. */
    virtual void Pause(bool /* paused */) {}
    /* A calibration of it has been changed under it. */
    virtual void OnCalibrationChanged() {}

    virtual std::string Describe() const = 0;

  protected:
    /* An event of this sensor at timestamp, its values zero. */
    sensors_event_t Event(int64_t timestamp) const;
    void Emit(const sensors_event_t& event, std::vector<sensors_event_t>* out);

    sensor_t info_ = {};
    bool ok_ = false;
    bool active_ = false;

  private:
    uint64_t events_ = 0;
};

/*
 * A sensor of the hub: an IIO device and its scan records. What a record
 * means is the subclass' to say.
 *
 * Several sensors can read one device -- the accelerometer and its
 * uncalibrated form do. The device then runs while any of them is on, at the
 * fastest rate any of them asks for, batched only if all of them allow it;
 * the first of them reads it and hands every record to each.
 */
class IioSensor : public Sensor {
  public:
    std::vector<int> Fds() const override;
    int Activate(bool enabled) override;
    int Batch(int64_t periodNs, int64_t latencyNs) override;
    int Flush(std::vector<sensors_event_t>* out) override;
    void OnReadable(int fd, std::vector<sensors_event_t>* out) override;
    void Pause(bool paused) override;
    std::string Describe() const override;

  protected:
    /* Opens the device and fills in what its rates and scale say. */
    bool Open(const char* iio, const char* name, int type, const char* stringType, float power);
    /* Reads the device another sensor has open. */
    bool Attach(const IioSensor& owner, const char* name, int type, const char* stringType,
                float power);
    /* One record's event; false to drop it. */
    virtual bool Convert(const uint8_t* record, sensors_event_t* event) = 0;

    IioDevice& device() { return shared_->device; }
    const IioDevice& device() const { return shared_->device; }

    /* The data channels in index order: all but the time and the accuracy. */
    std::vector<const IioChannel*> axes_;
    const IioChannel* accuracy_ = nullptr;
    const IioChannel* time_ = nullptr;

  private:
    struct Shared {
        IioDevice device;
        std::vector<IioSensor*> users;
        bool paused = false;
    };

    void Describe(const char* name, int type, const char* stringType, float power);
    /* Puts the device in the state its users' wishes add up to. */
    int Apply();
    void Dispatch(std::vector<sensors_event_t>* out);

    std::shared_ptr<Shared> shared_;
    int64_t periodNs_ = 0;
    int64_t latencyNs_ = 0;
};

/* Accelerometer, gyroscope, magnetometer: integers to scale. */
class AxisSensor : public IioSensor {
  public:
    /* unit takes the device's scaled unit to Android's: the magnetometer's
     * scale is in gauss, Android wants microtesla. */
    AxisSensor(const char* iio, const char* name, int type, const char* stringType,
                 float power, float unit);

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    float unit_;
};

/* Their uncalibrated forms: each word the reading below, the bias above. */
class UncalibratedAxisSensor : public AxisSensor {
  public:
    using AxisSensor::AxisSensor;

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
};

/* The hub's fused outputs, given as IEEE floats. */
class FusedSensor : public IioSensor {
  public:
    /* nineAxis: computed from all three chips, magnetometer included. */
    FusedSensor(const char* iio, const char* name, int type, const char* stringType,
                float power, float maxRange, float resolution, bool nineAxis = false);

    bool UsesNineAxisFusion() const override { return nineAxis_; }
    bool TakeFusionFault() override;

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    /* Counts a NaN record toward a fault; false for it. */
    bool Nan();

    bool nineAxis_;
    int nans_ = 0;
    bool fault_ = false;
};

/* The three rotation vectors: x, y, z, w as IEEE floats. */
class RotationSensor : public FusedSensor {
  public:
    /* heading: the fifth value, the heading's accuracy, is reported. */
    RotationSensor(const char* iio, const char* name, int type, const char* stringType, float power,
               bool heading, bool nineAxis = false);

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    bool heading_;
};

/* The steps since boot; the hub's count is 16 bits and wraps. */
class StepCounter : public IioSensor {
  public:
    StepCounter();
    int Activate(bool enabled) override;
    void OnActivated(std::vector<sensors_event_t>* out) override;

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    void Count(int raw);

    uint64_t steps_ = 0;
    int last_ = -1;
};

/*
 * A sensor that reports happenings, not samples: a step, a significant
 * motion. The hub's driver puts them on the device's event channel; the
 * buffer still has to run for the hub to have the sensor on, and what it
 * gives is drained and dropped.
 */
class EventSensor : public IioSensor {
  public:
    /* oneShot: off after it fires, as Android's one-shot sensors are. */
    EventSensor(const char* iio, const char* name, int type, const char* stringType,
                   float power, bool oneShot);

    std::vector<int> Fds() const override;
    int Flush(std::vector<sensors_event_t>* out) override;
    void OnReadable(int fd, std::vector<sensors_event_t>* out) override;

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    bool oneShot_;
};

/* The ambient light sensor, ISL29035, through the input layer in mlux. */
class LightSensor : public Sensor {
  public:
    LightSensor();

    std::vector<int> Fds() const override { return {input_.fd()}; }
    int Activate(bool enabled) override;
    int Batch(int64_t periodNs, int64_t latencyNs) override;
    void OnReadable(int fd, std::vector<sensors_event_t>* out) override;
    std::string Describe() const override;

  private:
    InputDevice input_;
    int mlux_ = 0;
};

/* ---- the sensors themselves ---- */

/*
 * How far off the accelerometer reads, measured rather than assumed: at rest
 * the only force on it is gravity, so the length of what it reads has to be
 * g, and how far it is from g is its error just then.
 *
 * The error is kept per pose -- which axis points down, and which way --
 * since an offset on an axis shows only when that axis bears the gravity:
 * lying flat hides one on X almost entirely. The accuracy is that of the
 * worst pose seen. Until it has seen one, it says nothing good.
 */
class AccelerometerAccuracy {
  public:
    void Add(const float a[3]);
    /* SENSOR_STATUS_ACCURACY_*. */
    int8_t status() const { return status_; }
    /* The worst error seen at rest, as a fraction of g; -1 if none yet. */
    float error() const { return worst_; }
    void Reset();

  private:
    static constexpr int kWindow = 16;

    float samples_[kWindow][3] = {};
    int count_ = 0;
    int next_ = 0;
    float poses_[6] = {-1, -1, -1, -1, -1, -1};
    float worst_ = -1;
    int8_t status_ = SENSOR_STATUS_ACCURACY_LOW;
};

/* Its offset is in the hub; its scale is applied here, and its accuracy is
 * measured as it runs. */
class Accelerometer : public AxisSensor {
  public:
    explicit Accelerometer(const HubCalibration* calibration);

    /* A new calibration makes what was measured of the old one moot. */
    void OnCalibrationChanged() override { measured_.Reset(); }
    std::string Describe() const override;
    const AccelerometerAccuracy& measured() const { return measured_; }

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    const HubCalibration* calibration_;
    AccelerometerAccuracy measured_;
};

/* The accelerometer as the chip reads it: the hub's offset added back, our
 * scale left off, and the offset given as the bias. */
class UncalibratedAccelerometer : public IioSensor {
  public:
    UncalibratedAccelerometer(const Accelerometer& accelerometer,
                              const HubCalibration* calibration);

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    const HubCalibration* calibration_;
};

class Gyroscope : public AxisSensor {
  public:
    Gyroscope();
};

class Magnetometer : public AxisSensor {
  public:
    Magnetometer();
};

class UncalibratedGyroscope : public UncalibratedAxisSensor {
  public:
    UncalibratedGyroscope();
};

class UncalibratedMagnetometer : public UncalibratedAxisSensor {
  public:
    UncalibratedMagnetometer();
};

class RotationVector : public RotationSensor {
  public:
    RotationVector();
};

class GameRotationVector : public RotationSensor {
  public:
    GameRotationVector();
};

class GeomagneticRotationVector : public RotationSensor {
  public:
    GeomagneticRotationVector();
};

class Gravity : public FusedSensor {
  public:
    Gravity();
};

class LinearAcceleration : public FusedSensor {
  public:
    LinearAcceleration();
};

class Orientation : public FusedSensor {
  public:
    Orientation();
};

class StepDetector : public EventSensor {
  public:
    StepDetector();
};

class SignificantMotion : public EventSensor {
  public:
    SignificantMotion();
};

/* Every sensor the board has, handles from 1 up in this order. */
std::vector<std::unique_ptr<Sensor>> MakeSensors(const HubCalibration* calibration);

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_SENSOR_H
