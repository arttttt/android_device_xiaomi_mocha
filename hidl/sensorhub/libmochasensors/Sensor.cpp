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

#define LOG_TAG "mochasensors"

#include "mochasensors/Sensor.h"

#include <errno.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

namespace mocha {
namespace sensors {

namespace {

constexpr float kGravity = 9.80665f;
constexpr const char* kVendorSt = "STMicroelectronics";
constexpr const char* kActivityRecognition = "android.permission.ACTIVITY_RECOGNITION";

int64_t Nanoseconds(const timeval& time) {
    return time.tv_sec * 1000000000LL + time.tv_usec * 1000LL;
}

}  // namespace

/* ---- Sensor ---- */

sensors_event_t Sensor::Event(int64_t timestamp) const {
    sensors_event_t event;
    memset(&event, 0, sizeof(event));
    event.version = sizeof(event);
    event.sensor = info_.handle;
    event.type = info_.type;
    event.timestamp = timestamp;
    return event;
}

void Sensor::Emit(const sensors_event_t& event, std::vector<sensors_event_t>* out) {
    out->push_back(event);
    events_++;
}

int Sensor::Flush(std::vector<sensors_event_t>* out) {
    if (!active_) return -EINVAL;

    sensors_event_t event;
    memset(&event, 0, sizeof(event));
    event.version = META_DATA_VERSION;
    event.type = SENSOR_TYPE_META_DATA;
    event.meta_data.what = META_DATA_FLUSH_COMPLETE;
    event.meta_data.sensor = info_.handle;
    out->push_back(event);
    return 0;
}

/* ---- IioSensor ---- */

void IioSensor::Describe(const char* name, int type, const char* stringType, float power) {
    info_.name = name;
    info_.vendor = kVendorSt;
    info_.version = 1;
    info_.type = type;
    info_.stringType = stringType;
    info_.power = power;
    info_.flags = SENSOR_FLAG_CONTINUOUS_MODE;

    const auto& rates = device().rates();
    if (!rates.empty()) {
        info_.minDelay = 1000000 / rates.back();
        info_.maxDelay = 1000000 / rates.front();
    }
    /* The FIFO is the hub's, shared by every sensor in it: none has any
     * of it to itself. */
    if (device().batchable()) info_.fifoMaxEventCount = device().fifoMaxEvents();
}

bool IioSensor::Open(const char* iio, const char* name, int type, const char* stringType,
                     float power) {
    shared_ = std::make_shared<Shared>();
    shared_->users.push_back(this);
    if (!device().Open(iio)) {
        LOG(WARNING) << "no " << iio;
        return false;
    }

    for (const auto& channel : device().channels()) {
        if (channel.name == "in_timestamp")
            time_ = &channel;
        else if (channel.name.find("accuracy") != std::string::npos)
            accuracy_ = &channel;
        else if (channel.storageBytes > 0)
            axes_.push_back(&channel);
    }
    Describe(name, type, stringType, power);
    ok_ = true;
    return true;
}

bool IioSensor::Attach(const IioSensor& owner, const char* name, int type,
                       const char* stringType, float power) {
    if (!owner.ok()) return false;
    shared_ = owner.shared_;
    shared_->users.push_back(this);
    axes_ = owner.axes_;
    accuracy_ = owner.accuracy_;
    time_ = owner.time_;
    Describe(name, type, stringType, power);
    ok_ = true;
    return true;
}

/* The first user reads the device for all of them. */
std::vector<int> IioSensor::Fds() const {
    if (shared_->users.front() != this) return {};
    return {device().fd()};
}

int IioSensor::Activate(bool enabled) {
    if (enabled == active_) return 0;
    active_ = enabled;
    int err = Apply();
    if (err) active_ = !enabled;
    return err;
}

int IioSensor::Batch(int64_t periodNs, int64_t latencyNs) {
    /* Clamped to what the sensor can do, as Android allows. */
    int64_t fastest = int64_t{info_.minDelay} * 1000;
    int64_t slowest = int64_t{info_.maxDelay} * 1000;
    if (periodNs < fastest) periodNs = fastest;
    if (slowest > 0 && periodNs > slowest) periodNs = slowest;
    periodNs_ = periodNs;
    latencyNs_ = latencyNs;
    return Apply();
}

int IioSensor::Apply() {
    bool on = false;
    int64_t period = 0, latency = INT64_MAX;
    for (const IioSensor* user : shared_->users) {
        if (!user->active_) continue;
        on = true;
        if (user->periodNs_ > 0 && (period == 0 || user->periodNs_ < period)) period = user->periodNs_;
        latency = std::min(latency, user->latencyNs_);
    }
    /* With none on, the last one to ask sets what the next start has. */
    if (!on) {
        period = periodNs_;
        latency = latencyNs_;
    }

    /*
     * Below 300 ms the driver takes no timeout, and a latency that short
     * saves little anyway: such a sensor reports sample by sample. The
     * FIFO is read out on the timeout, or by the hub itself when it fills.
     */
    constexpr int64_t kShortestBatchNs = 300000000;
    bool batch = device().batchable() && latency >= kShortestBatchNs;
    int timeoutMs = static_cast<int>(std::min<int64_t>(latency / 1000000, 600000));
    if (!device().SetBatch(batch, timeoutMs)) return -EIO;

    /* The rate at or above the asked one: a sensor may report faster than
     * asked, never slower. A period is often 1 s / rate cut to whole
     * nanoseconds, a hair short, so the asked rate is rounded down first
     * by a part in a thousand rather than up to the next one. */
    if (period > 0) {
        int hz = static_cast<int>(999000000LL / period + 1);
        if (999000000LL % period == 0) hz--;
        if (!device().SetRate(hz)) return -EIO;
    }

    return device().SetEnabled(on && !shared_->paused) ? 0 : -EIO;
}

void IioSensor::Pause(bool paused) {
    if (shared_->paused == paused) return;
    shared_->paused = paused;
    Apply();
}

/*
 * A batching sensor's samples wait in the hub's FIFO. The hub's driver
 * returns from its flush once the FIFO has been pushed to the buffers, so
 * reading the buffer after it gets all a flush covers, and the completion
 * goes after that.
 */
int IioSensor::Flush(std::vector<sensors_event_t>* out) {
    if (!active_) return -EINVAL;
    if (device().batching()) {
        if (!IioDevice::FlushHub()) LOG(WARNING) << device().name() << ": the hub's flush failed";
        Dispatch(out);
    }
    return Sensor::Flush(out);
}

void IioSensor::OnReadable(int /* fd */, std::vector<sensors_event_t>* out) {
    Dispatch(out);
}

void IioSensor::Dispatch(std::vector<sensors_event_t>* out) {
    /* A batch's read-out comes in at once: up to a FIFO's worth. */
    uint8_t buffer[4096];
    size_t bytes = device().recordBytes();
    size_t size = sizeof(buffer) - sizeof(buffer) % bytes;

    ssize_t got;
    while ((got = device().Read(buffer, size)) > 0) {
        for (ssize_t at = 0; at < got; at += bytes) {
            int64_t timestamp = time_ ? IioDevice::Raw(*time_, buffer + at) : 0;
            /* What was left in the buffer from before a user was on is
             * not that user's. */
            for (IioSensor* user : shared_->users) {
                if (!user->active_) continue;
                sensors_event_t event = user->Event(timestamp);
                if (user->Convert(buffer + at, &event)) user->Emit(event, out);
            }
        }
    }
}

std::string IioSensor::Describe() const {
    return android::base::StringPrintf("%-18s %s %3d Hz %s", device().name().c_str(),
                                       active_ ? "on " : "off", device().rate(),
                                       device().batching() ? "batch" : "");
}

/* ---- AxisSensor, UncalibratedAxisSensor ---- */

AxisSensor::AxisSensor(const char* iio, const char* name, int type, const char* stringType,
                           float power, float unit)
    : unit_(unit) {
    if (!Open(iio, name, type, stringType, power) || axes_.empty()) return;
    float step = axes_[0]->scale * unit_;
    info_.resolution = step;
    info_.maxRange = step * 32767;
}

bool AxisSensor::Convert(const uint8_t* record, sensors_event_t* event) {
    for (size_t i = 0; i < 3 && i < axes_.size(); i++)
        event->data[i] = IioDevice::Raw(*axes_[i], record) * axes_[i]->scale * unit_;
    event->acceleration.status = accuracy_ ? static_cast<int8_t>(IioDevice::Raw(*accuracy_, record))
                                           : SENSOR_STATUS_ACCURACY_HIGH;
    return true;
}

bool UncalibratedAxisSensor::Convert(const uint8_t* record, sensors_event_t* event) {
    for (size_t i = 0; i < 3 && i < axes_.size(); i++) {
        int64_t both = IioDevice::Raw(*axes_[i], record);
        float step = axes_[i]->scale * unit_;
        event->uncalibrated_gyro.uncalib[i] = static_cast<int16_t>(both & 0xffff) * step;
        event->uncalibrated_gyro.bias[i] = static_cast<int16_t>((both >> 16) & 0xffff) * step;
    }
    return true;
}

/* ---- the accelerometer ---- */

void AccelerometerAccuracy::Reset() {
    count_ = next_ = 0;
    for (float& pose : poses_) pose = -1;
    worst_ = -1;
    status_ = SENSOR_STATUS_ACCURACY_LOW;
}

void AccelerometerAccuracy::Add(const float a[3]) {
    for (int i = 0; i < 3; i++) samples_[next_][i] = a[i];
    next_ = (next_ + 1) % kWindow;
    if (count_ < kWindow) {
        count_++;
        return;
    }

    /* At rest: the window's lengths all within a hand's tremor of one
     * another. 16 samples is about a second at the rate auto-rotation
     * keeps it at. */
    constexpr float kStill = 0.03f;
    float mean[3] = {}, low = 1e9f, high = 0;
    for (const auto& sample : samples_) {
        float length = std::sqrt(sample[0] * sample[0] + sample[1] * sample[1] +
                                 sample[2] * sample[2]);
        low = std::min(low, length);
        high = std::max(high, length);
        for (int i = 0; i < 3; i++) mean[i] += sample[i] / kWindow;
    }
    if (high - low > kStill) return;

    float length = std::sqrt(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]);
    float error = std::fabs(length / kGravity - 1);

    /* The pose: the axis bearing most of the gravity, and its sign. */
    int axis = 0;
    for (int i = 1; i < 3; i++)
        if (std::fabs(mean[i]) > std::fabs(mean[axis])) axis = i;
    poses_[axis * 2 + (mean[axis] < 0 ? 1 : 0)] = error;

    worst_ = 0;
    for (float pose : poses_) worst_ = std::max(worst_, pose);
    status_ = worst_ <= 0.005f   ? SENSOR_STATUS_ACCURACY_HIGH
              : worst_ <= 0.015f ? SENSOR_STATUS_ACCURACY_MEDIUM
                                 : SENSOR_STATUS_ACCURACY_LOW;
}

Accelerometer::Accelerometer(const HubCalibration* calibration)
    : AxisSensor("lsm6db0_accel", "Accelerometer", SENSOR_TYPE_ACCELEROMETER,
                 SENSOR_STRING_TYPE_ACCELEROMETER, 0.25f, 1.0f),
      calibration_(calibration) {}

bool Accelerometer::Convert(const uint8_t* record, sensors_event_t* event) {
    AxisSensor::Convert(record, event);
    const AccelCalibration& cal = calibration_->accel();
    for (int i = 0; i < 3; i++) event->acceleration.v[i] *= cal.scale[i];
    measured_.Add(event->acceleration.v);
    event->acceleration.status = measured_.status();
    return true;
}

std::string Accelerometer::Describe() const {
    const AccelCalibration& cal = calibration_->accel();
    std::string out = IioSensor::Describe();
    android::base::StringAppendF(&out, " cal %s offset %d %d %d scale %.4f %.4f %.4f",
                                 cal.present ? "yes" : "no", cal.offset[0], cal.offset[1],
                                 cal.offset[2], cal.scale[0], cal.scale[1], cal.scale[2]);
    if (measured_.error() >= 0)
        android::base::StringAppendF(&out, " error %.2f%% status %d", measured_.error() * 100,
                                     measured_.status());
    return out;
}

UncalibratedAccelerometer::UncalibratedAccelerometer(const Accelerometer& accelerometer,
                                                     const HubCalibration* calibration)
    : calibration_(calibration) {
    if (!Attach(accelerometer, "Accelerometer Uncalibrated", SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED,
                SENSOR_STRING_TYPE_ACCELEROMETER_UNCALIBRATED, 0.25f) ||
        axes_.empty())
        return;
    info_.resolution = axes_[0]->scale;
    info_.maxRange = axes_[0]->scale * 32767;
}

/* What the hub gives has our offset taken off already: it goes back on, and
 * is the bias. Our scale is the calibrated sensor's alone. */
bool UncalibratedAccelerometer::Convert(const uint8_t* record, sensors_event_t* event) {
    const AccelCalibration& cal = calibration_->accel();
    for (size_t i = 0; i < 3 && i < axes_.size(); i++) {
        float step = axes_[i]->scale;
        event->uncalibrated_accelerometer.bias[i] = cal.offset[i] * step;
        event->uncalibrated_accelerometer.uncalib[i] =
                IioDevice::Raw(*axes_[i], record) * step + cal.offset[i] * step;
    }
    return true;
}

/* ---- FusedSensor, RotationSensor ---- */

FusedSensor::FusedSensor(const char* iio, const char* name, int type, const char* stringType,
                         float power, float maxRange, float resolution, bool nineAxis)
    : nineAxis_(nineAxis) {
    if (!Open(iio, name, type, stringType, power)) return;
    info_.maxRange = maxRange;
    info_.resolution = resolution;
}

/*
 * The hub's nine-axis fusion has been seen to put out nothing but NaN after
 * a boot, until its magnetometer calibration is started over. NaN goes to
 * nobody; a run of them is a fault the hub is told to fix.
 */
bool FusedSensor::Nan() {
    constexpr int kFaultAfter = 10;
    if (++nans_ == kFaultAfter) fault_ = true;
    return false;
}

bool FusedSensor::TakeFusionFault() {
    bool fault = fault_;
    fault_ = false;
    if (fault) nans_ = 0;
    return fault;
}

bool FusedSensor::Convert(const uint8_t* record, sensors_event_t* event) {
    for (size_t i = 0; i < 3 && i < axes_.size(); i++) {
        event->data[i] = IioDevice::Float(*axes_[i], record);
        if (std::isnan(event->data[i])) return Nan();
    }
    nans_ = 0;
    event->orientation.status = accuracy_ ? static_cast<int8_t>(IioDevice::Raw(*accuracy_, record))
                                          : SENSOR_STATUS_ACCURACY_HIGH;
    return true;
}

RotationSensor::RotationSensor(const char* iio, const char* name, int type, const char* stringType,
                       float power, bool heading, bool nineAxis)
    : FusedSensor(iio, name, type, stringType, power, 1.0f, 1.0f / 65536, nineAxis),
      heading_(heading) {}

bool RotationSensor::Convert(const uint8_t* record, sensors_event_t* event) {
    float q[4] = {};
    for (size_t i = 0; i < 4 && i < axes_.size(); i++) {
        q[i] = IioDevice::Float(*axes_[i], record);
        if (std::isnan(q[i])) return Nan();
    }
    nans_ = 0;
    /* -q is the same rotation; Android's readers take w >= 0. */
    float sign = q[3] < 0 ? -1.0f : 1.0f;
    for (int i = 0; i < 4; i++) event->data[i] = sign * q[i];
    /* No estimate of the heading's accuracy is given. */
    if (heading_) event->data[4] = -1.0f;
    return true;
}

/* ---- StepCounter ---- */

StepCounter::StepCounter() {
    if (!Open("lsm6db0_step_c", "Step Counter", SENSOR_TYPE_STEP_COUNTER,
              SENSOR_STRING_TYPE_STEP_COUNTER, 0.01f))
        return;
    info_.flags = SENSOR_FLAG_ON_CHANGE_MODE;
    info_.requiredPermission = kActivityRecognition;
    info_.maxRange = 4294967295.0f;
    info_.resolution = 1.0f;
    info_.minDelay = 0;
    info_.maxDelay = 0;
}

/*
 * The hub sends a count only on a step, and an on-change sensor owes its
 * current value as soon as it is on: the count is read once through sysfs
 * first, which the driver allows only while the buffer is off.
 */
int StepCounter::Activate(bool enabled) {
    int64_t raw;
    if (enabled && !active_ && device().ReadRaw("in_step_counter", &raw)) Count(static_cast<int>(raw));
    return IioSensor::Activate(enabled);
}

void StepCounter::OnActivated(std::vector<sensors_event_t>* out) {
    if (last_ < 0) return;
    timespec now;
    clock_gettime(CLOCK_BOOTTIME, &now);
    sensors_event_t event = Event(now.tv_sec * 1000000000LL + now.tv_nsec);
    event.u64.step_counter = steps_;
    Emit(event, out);
}

void StepCounter::Count(int raw) {
    raw = static_cast<uint16_t>(raw);
    if (last_ < 0)
        steps_ = raw;
    else
        steps_ += static_cast<uint16_t>(raw - last_);
    last_ = raw;
}

bool StepCounter::Convert(const uint8_t* record, sensors_event_t* event) {
    if (axes_.empty()) return false;
    Count(static_cast<int>(IioDevice::Raw(*axes_[0], record)));
    event->u64.step_counter = steps_;
    return true;
}

/* ---- EventSensor ---- */

EventSensor::EventSensor(const char* iio, const char* name, int type,
                               const char* stringType, float power, bool oneShot)
    : oneShot_(oneShot) {
    if (!Open(iio, name, type, stringType, power)) return;
    if (device().OpenEvents() < 0) {
        ok_ = false;
        return;
    }
    info_.flags = oneShot ? SENSOR_FLAG_ONE_SHOT_MODE : SENSOR_FLAG_SPECIAL_REPORTING_MODE;
    info_.minDelay = oneShot ? -1 : 0;
    info_.maxDelay = 0;
    info_.maxRange = 1.0f;
    info_.resolution = 1.0f;
    if (type == SENSOR_TYPE_STEP_DETECTOR) info_.requiredPermission = kActivityRecognition;
}

std::vector<int> EventSensor::Fds() const {
    return {device().fd(), device().eventFd()};
}

/* A one-shot sensor has nothing to flush, and Android says to refuse. */
int EventSensor::Flush(std::vector<sensors_event_t>* out) {
    if (oneShot_) return -EINVAL;
    if (!active_) return -EINVAL;
    /* Batched, its happenings come out of the FIFO onto the event channel. */
    if (device().batching()) {
        if (!IioDevice::FlushHub()) LOG(WARNING) << device().name() << ": the hub's flush failed";
        OnReadable(device().eventFd(), out);
    }
    return Sensor::Flush(out);
}

void EventSensor::OnReadable(int fd, std::vector<sensors_event_t>* out) {
    if (fd != device().eventFd()) {
        uint8_t drain[256];
        while (device().Read(drain, sizeof(drain)) > 0) {
        }
        return;
    }

    int64_t timestamp;
    while (device().ReadEvent(&timestamp)) {
        if (!active_) continue;
        sensors_event_t event = Event(timestamp);
        event.data[0] = 1.0f;
        Emit(event, out);
        if (oneShot_) Activate(false);
    }
}

bool EventSensor::Convert(const uint8_t* /* record */, sensors_event_t* /* event */) {
    return false;
}

/* ---- LightSensor ---- */

LightSensor::LightSensor() {
    info_.name = "Ambient Light";
    info_.vendor = "Intersil";
    info_.version = 1;
    info_.type = SENSOR_TYPE_LIGHT;
    info_.stringType = SENSOR_STRING_TYPE_LIGHT;
    info_.power = 0.09f;
    info_.flags = SENSOR_FLAG_ON_CHANGE_MODE;

    if (!input_.Open("isl29035")) {
        LOG(WARNING) << "no isl29035";
        return;
    }
    info_.maxRange = input_.maximum() > 0 ? input_.maximum() / 1000.0f : 30000.0f;
    /* A 16-bit conversion across the range it is in. */
    info_.resolution = info_.maxRange / 65535;
    ok_ = true;
}

int LightSensor::Activate(bool enabled) {
    if (enabled == active_) return 0;
    if (!input_.SetEnabled(enabled)) return -EIO;
    active_ = enabled;
    return 0;
}

/* It interrupts on a change: there is no rate to set and nothing kept to
 * batch. */
int LightSensor::Batch(int64_t /* periodNs */, int64_t /* latencyNs */) {
    return 0;
}

void LightSensor::OnReadable(int /* fd */, std::vector<sensors_event_t>* out) {
    input_event input;
    while (input_.Read(&input)) {
        if (input.type == EV_ABS && input.code == ABS_MISC) {
            mlux_ = input.value;
        } else if (input.type == EV_SYN && input.code == SYN_REPORT && active_) {
            sensors_event_t event = Event(Nanoseconds(input.time));
            event.light = mlux_ / 1000.0f;
            Emit(event, out);
        }
    }
}

std::string LightSensor::Describe() const {
    return android::base::StringPrintf("%-18s %s", "isl29035", active_ ? "on " : "off");
}

/* ---- the sensors themselves ----
 *
 * Power in mA: ST gives no figure per output of the hub, so these are the
 * chips' figures added up for what each output runs. */

Gyroscope::Gyroscope()
    : AxisSensor("lsm6db0_gyro", "Gyroscope", SENSOR_TYPE_GYROSCOPE,
                 SENSOR_STRING_TYPE_GYROSCOPE, 3.0f, 1.0f) {}

/* The hub's scale is in gauss; Android wants microtesla. */
Magnetometer::Magnetometer()
    : AxisSensor("lsm6db0_magn", "Magnetometer", SENSOR_TYPE_MAGNETIC_FIELD,
                 SENSOR_STRING_TYPE_MAGNETIC_FIELD, 0.6f, 100.0f) {}

UncalibratedGyroscope::UncalibratedGyroscope()
    : UncalibratedAxisSensor("lsm6db0_gyro_u", "Gyroscope Uncalibrated",
                             SENSOR_TYPE_GYROSCOPE_UNCALIBRATED,
                             SENSOR_STRING_TYPE_GYROSCOPE_UNCALIBRATED, 3.0f, 1.0f) {}

UncalibratedMagnetometer::UncalibratedMagnetometer()
    : UncalibratedAxisSensor("lsm6db0_magn_u", "Magnetometer Uncalibrated",
                             SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED,
                             SENSOR_STRING_TYPE_MAGNETIC_FIELD_UNCALIBRATED, 0.6f, 100.0f) {}

RotationVector::RotationVector()
    : RotationSensor("lsm6db0_quat", "Rotation Vector", SENSOR_TYPE_ROTATION_VECTOR,
                     SENSOR_STRING_TYPE_ROTATION_VECTOR, 3.85f, true, true) {}

GameRotationVector::GameRotationVector()
    : RotationSensor("lsm6db0_game_quat", "Game Rotation Vector",
                     SENSOR_TYPE_GAME_ROTATION_VECTOR, SENSOR_STRING_TYPE_GAME_ROTATION_VECTOR,
                     3.25f, false) {}

GeomagneticRotationVector::GeomagneticRotationVector()
    : RotationSensor("lsm6db0_geo_quat", "Geomagnetic Rotation Vector",
                     SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR,
                     SENSOR_STRING_TYPE_GEOMAGNETIC_ROTATION_VECTOR, 0.85f, true) {}

Gravity::Gravity()
    : FusedSensor("lsm6db0_gravity", "Gravity", SENSOR_TYPE_GRAVITY, SENSOR_STRING_TYPE_GRAVITY,
                  3.25f, 2 * kGravity, 1.0f / 1024) {}

LinearAcceleration::LinearAcceleration()
    : FusedSensor("lsm6db0_linear", "Linear Acceleration", SENSOR_TYPE_LINEAR_ACCELERATION,
                  SENSOR_STRING_TYPE_LINEAR_ACCELERATION, 3.25f, 2 * kGravity, 1.0f / 1024) {}

/* Azimuth, pitch and roll in degrees, as the hub gives them. */
Orientation::Orientation()
    : FusedSensor("lsm6db0_euler", "Orientation", SENSOR_TYPE_ORIENTATION,
                  SENSOR_STRING_TYPE_ORIENTATION, 3.85f, 360.0f, 1.0f / 64, true) {}

StepDetector::StepDetector()
    : EventSensor("lsm6db0_step_d", "Step Detector", SENSOR_TYPE_STEP_DETECTOR,
                  SENSOR_STRING_TYPE_STEP_DETECTOR, 0.01f, false) {}

SignificantMotion::SignificantMotion()
    : EventSensor("lsm6db0_sign_motion", "Significant Motion", SENSOR_TYPE_SIGNIFICANT_MOTION,
                  SENSOR_STRING_TYPE_SIGNIFICANT_MOTION, 0.25f, true) {}

/* ---- the board ---- */

std::vector<std::unique_ptr<Sensor>> MakeSensors(const HubCalibration* calibration) {
    std::vector<std::unique_ptr<Sensor>> all;
    auto accelerometer = std::make_unique<Accelerometer>(calibration);
    auto uncalibrated = std::make_unique<UncalibratedAccelerometer>(*accelerometer, calibration);
    all.push_back(std::move(accelerometer));
    all.push_back(std::move(uncalibrated));
    all.push_back(std::make_unique<Magnetometer>());
    all.push_back(std::make_unique<UncalibratedMagnetometer>());
    all.push_back(std::make_unique<Gyroscope>());
    all.push_back(std::make_unique<UncalibratedGyroscope>());
    all.push_back(std::make_unique<RotationVector>());
    all.push_back(std::make_unique<GameRotationVector>());
    all.push_back(std::make_unique<GeomagneticRotationVector>());
    all.push_back(std::make_unique<Gravity>());
    all.push_back(std::make_unique<LinearAcceleration>());
    all.push_back(std::make_unique<Orientation>());
    all.push_back(std::make_unique<StepCounter>());
    all.push_back(std::make_unique<StepDetector>());
    all.push_back(std::make_unique<SignificantMotion>());
    all.push_back(std::make_unique<LightSensor>());

    std::vector<std::unique_ptr<Sensor>> present;
    for (auto& sensor : all) {
        if (!sensor->ok()) continue;
        sensor->SetHandle(static_cast<int>(present.size()) + 1);
        present.push_back(std::move(sensor));
    }
    return present;
}

}  // namespace sensors
}  // namespace mocha
