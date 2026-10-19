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

bool IioSensor::Open(const char* iio, const char* name, int type, const char* stringType,
                     float power) {
    info_.name = name;
    info_.vendor = kVendorSt;
    info_.version = 1;
    info_.type = type;
    info_.stringType = stringType;
    info_.power = power;
    info_.flags = SENSOR_FLAG_CONTINUOUS_MODE;

    if (!device_.Open(iio)) {
        LOG(WARNING) << "no " << iio;
        return false;
    }

    for (const auto& channel : device_.channels()) {
        if (channel.name == "in_timestamp")
            time_ = &channel;
        else if (channel.name.find("accuracy") != std::string::npos)
            accuracy_ = &channel;
        else if (channel.storageBytes > 0)
            axes_.push_back(&channel);
    }

    const auto& rates = device_.rates();
    if (!rates.empty()) {
        info_.minDelay = 1000000 / rates.back();
        info_.maxDelay = 1000000 / rates.front();
    }
    ok_ = true;
    return true;
}

int IioSensor::Activate(bool enabled) {
    if (enabled == active_) return 0;
    if (enabled && periodNs_ > 0) SetPeriod(periodNs_);
    if (!device_.SetEnabled(enabled)) return -EIO;
    active_ = enabled;
    return 0;
}

int IioSensor::SetPeriod(int64_t ns) {
    /* Clamped to what the sensor can do, as Android allows. */
    int64_t fastest = int64_t{info_.minDelay} * 1000;
    int64_t slowest = int64_t{info_.maxDelay} * 1000;
    if (ns < fastest) ns = fastest;
    if (slowest > 0 && ns > slowest) ns = slowest;
    periodNs_ = ns;
    if (ns <= 0) return 0;

    /* The rate at or above the asked one: a sensor may report faster than
     * asked, never slower. */
    int hz = static_cast<int>((1000000000 + ns - 1) / ns);
    return device_.SetRate(hz) ? 0 : -EIO;
}

void IioSensor::OnReadable(int /* fd */, std::vector<sensors_event_t>* out) {
    constexpr size_t kRecords = 16;
    uint8_t buffer[64 * kRecords];
    size_t size = std::min(sizeof(buffer), device_.recordBytes() * kRecords);

    ssize_t got = device_.Read(buffer, size);
    for (ssize_t at = 0; at + static_cast<ssize_t>(device_.recordBytes()) <= got;
         at += device_.recordBytes()) {
        sensors_event_t event = Event(time_ ? IioDevice::Raw(*time_, buffer + at) : 0);
        if (Convert(buffer + at, &event)) Emit(event, out);
    }
}

std::string IioSensor::Describe() const {
    return android::base::StringPrintf("%-18s %s %3d Hz", device_.name().c_str(),
                                       active_ ? "on " : "off", device_.rate());
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

/* ---- FusedSensor, RotationSensor ---- */

FusedSensor::FusedSensor(const char* iio, const char* name, int type, const char* stringType,
                         float power, float maxRange, float resolution) {
    if (!Open(iio, name, type, stringType, power)) return;
    info_.maxRange = maxRange;
    info_.resolution = resolution;
}

bool FusedSensor::Convert(const uint8_t* record, sensors_event_t* event) {
    for (size_t i = 0; i < 3 && i < axes_.size(); i++) {
        event->data[i] = IioDevice::Float(*axes_[i], record);
        /* The hub's fusion has been seen to put out NaN; nothing downstream
         * takes it. */
        if (std::isnan(event->data[i])) return false;
    }
    event->orientation.status = accuracy_ ? static_cast<int8_t>(IioDevice::Raw(*accuracy_, record))
                                          : SENSOR_STATUS_ACCURACY_HIGH;
    return true;
}

RotationSensor::RotationSensor(const char* iio, const char* name, int type, const char* stringType,
                       float power, bool heading)
    : FusedSensor(iio, name, type, stringType, power, 1.0f, 1.0f / 65536), heading_(heading) {}

bool RotationSensor::Convert(const uint8_t* record, sensors_event_t* event) {
    float q[4] = {};
    for (size_t i = 0; i < 4 && i < axes_.size(); i++) {
        q[i] = IioDevice::Float(*axes_[i], record);
        if (std::isnan(q[i])) return false;
    }
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

/* It counts at the pace of the steps; there is no rate to set. */
int StepCounter::SetPeriod(int64_t /* ns */) {
    return 0;
}

bool StepCounter::Convert(const uint8_t* record, sensors_event_t* event) {
    if (axes_.empty()) return false;
    int raw = static_cast<uint16_t>(IioDevice::Raw(*axes_[0], record));
    if (last_ < 0)
        steps_ = raw;
    else
        steps_ += static_cast<uint16_t>(raw - last_);
    last_ = raw;
    event->u64.step_counter = steps_;
    return true;
}

/* ---- EventSensor ---- */

EventSensor::EventSensor(const char* iio, const char* name, int type,
                               const char* stringType, float power, bool oneShot)
    : oneShot_(oneShot) {
    if (!Open(iio, name, type, stringType, power)) return;
    if (device_.OpenEvents() < 0) {
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
    return {device_.fd(), device_.eventFd()};
}

int EventSensor::SetPeriod(int64_t /* ns */) {
    return 0;
}

/* A one-shot sensor has nothing to flush, and Android says to refuse. */
int EventSensor::Flush(std::vector<sensors_event_t>* out) {
    if (oneShot_) return -EINVAL;
    return IioSensor::Flush(out);
}

void EventSensor::OnReadable(int fd, std::vector<sensors_event_t>* out) {
    if (fd != device_.eventFd()) {
        uint8_t drain[256];
        while (device_.Read(drain, sizeof(drain)) > 0) {
        }
        return;
    }

    int64_t timestamp;
    while (device_.ReadEvent(&timestamp)) {
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

/* It interrupts on a change; there is no rate to set. */
int LightSensor::SetPeriod(int64_t /* ns */) {
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

Accelerometer::Accelerometer()
    : AxisSensor("lsm6db0_accel", "Accelerometer", SENSOR_TYPE_ACCELEROMETER,
                 SENSOR_STRING_TYPE_ACCELEROMETER, 0.25f, 1.0f) {}

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
                     SENSOR_STRING_TYPE_ROTATION_VECTOR, 3.85f, true) {}

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
                  SENSOR_STRING_TYPE_ORIENTATION, 3.85f, 360.0f, 1.0f / 64) {}

StepDetector::StepDetector()
    : EventSensor("lsm6db0_step_d", "Step Detector", SENSOR_TYPE_STEP_DETECTOR,
                  SENSOR_STRING_TYPE_STEP_DETECTOR, 0.01f, false) {}

SignificantMotion::SignificantMotion()
    : EventSensor("lsm6db0_sign_motion", "Significant Motion", SENSOR_TYPE_SIGNIFICANT_MOTION,
                  SENSOR_STRING_TYPE_SIGNIFICANT_MOTION, 0.25f, true) {}

/* ---- the board ---- */

std::vector<std::unique_ptr<Sensor>> MakeSensors() {
    std::vector<std::unique_ptr<Sensor>> all;
    all.push_back(std::make_unique<Accelerometer>());
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
