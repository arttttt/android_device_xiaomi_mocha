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
    virtual int SetPeriod(int64_t ns) = 0;
    /* Nothing is held back by default, so all there is has been delivered
     * when the call comes. */
    virtual int Flush(std::vector<sensors_event_t>* out);
    /* What a sensor reports as soon as it is switched on, if anything: an
     * on-change sensor's current value. */
    virtual void OnActivated(std::vector<sensors_event_t>* /* out */) {}
    virtual void OnReadable(int fd, std::vector<sensors_event_t>* out) = 0;

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
 */
class IioSensor : public Sensor {
  public:
    std::vector<int> Fds() const override { return {device_.fd()}; }
    int Activate(bool enabled) override;
    int SetPeriod(int64_t ns) override;
    void OnReadable(int fd, std::vector<sensors_event_t>* out) override;
    std::string Describe() const override;

  protected:
    /* Opens the device and fills in what its rates and scale say. */
    bool Open(const char* iio, const char* name, int type, const char* stringType, float power);
    /* One record's event; false to drop it. */
    virtual bool Convert(const uint8_t* record, sensors_event_t* event) = 0;

    /* The data channels in index order: all but the time and the accuracy. */
    std::vector<const IioChannel*> axes_;
    const IioChannel* accuracy_ = nullptr;
    const IioChannel* time_ = nullptr;
    IioDevice device_;
    int64_t periodNs_ = 0;
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
    FusedSensor(const char* iio, const char* name, int type, const char* stringType,
                float power, float maxRange, float resolution);

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
};

/* The three rotation vectors: x, y, z, w as IEEE floats. */
class RotationSensor : public FusedSensor {
  public:
    /* heading: the fifth value, the heading's accuracy, is reported. */
    RotationSensor(const char* iio, const char* name, int type, const char* stringType, float power,
               bool heading);

  protected:
    bool Convert(const uint8_t* record, sensors_event_t* event) override;
    bool heading_;
};

/* The steps since boot; the hub's count is 16 bits and wraps. */
class StepCounter : public IioSensor {
  public:
    StepCounter();
    int Activate(bool enabled) override;
    int SetPeriod(int64_t ns) override;
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
    int SetPeriod(int64_t ns) override;
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
    int SetPeriod(int64_t ns) override;
    void OnReadable(int fd, std::vector<sensors_event_t>* out) override;
    std::string Describe() const override;

  private:
    InputDevice input_;
    int mlux_ = 0;
};

/* ---- the sensors themselves ---- */

class Accelerometer : public AxisSensor {
  public:
    Accelerometer();
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
std::vector<std::unique_ptr<Sensor>> MakeSensors();

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_SENSOR_H
