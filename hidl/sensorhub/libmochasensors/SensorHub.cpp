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

#include "mochasensors/SensorHub.h"

#include <errno.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cmath>
#include <cstring>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

namespace mocha {
namespace sensors {

/* How the hub's record for a sensor turns into Android's values. */
enum class Kind {
    /* x, y, z as integers to scale; maybe an accuracy. */
    kVector,
    /* x, y, z each as two halves: the reading below, the bias above. */
    kUncalibrated,
    /* x, y, z, w as IEEE floats; maybe an accuracy. */
    kQuaternion,
    /* x, y, z as IEEE floats; maybe an accuracy. */
    kFloats,
};

struct SensorHub::Spec {
    const char* iio;
    const char* name;
    int type;
    const char* stringType;
    Kind kind;
    /* From the device's scaled unit to Android's: the magnetometer's
     * scale is in gauss, Android wants microtesla. */
    float unit;
    /* For the readings that are no scaled integers. */
    float maxRange;
    float resolution;
    /* mA; ST gives no figure per output of the hub, so these are the
     * chips' figures added up for what each output runs. */
    float power;
    /* The quaternion's fifth value, the heading accuracy, is reported. */
    bool heading;
};

namespace {

constexpr float kGravity = 9.80665f;

/*
 * What the hub hands over, one IIO device each. The vendor is ST's for all
 * of them: the hub computes even what Android has its own fusion for.
 */
const SensorHub::Spec kSpecs[] = {
        {"lsm6db0_accel", "Accelerometer", SENSOR_TYPE_ACCELEROMETER,
         SENSOR_STRING_TYPE_ACCELEROMETER, Kind::kVector, 1.0f, 0, 0, 0.25f, false},
        {"lsm6db0_magn", "Magnetometer", SENSOR_TYPE_MAGNETIC_FIELD,
         SENSOR_STRING_TYPE_MAGNETIC_FIELD, Kind::kVector, 100.0f, 0, 0, 0.6f, false},
        {"lsm6db0_magn_u", "Magnetometer Uncalibrated", SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED,
         SENSOR_STRING_TYPE_MAGNETIC_FIELD_UNCALIBRATED, Kind::kUncalibrated, 100.0f, 0, 0, 0.6f,
         false},
        {"lsm6db0_gyro", "Gyroscope", SENSOR_TYPE_GYROSCOPE, SENSOR_STRING_TYPE_GYROSCOPE,
         Kind::kVector, 1.0f, 0, 0, 3.0f, false},
        {"lsm6db0_gyro_u", "Gyroscope Uncalibrated", SENSOR_TYPE_GYROSCOPE_UNCALIBRATED,
         SENSOR_STRING_TYPE_GYROSCOPE_UNCALIBRATED, Kind::kUncalibrated, 1.0f, 0, 0, 3.0f, false},
        {"lsm6db0_quat", "Rotation Vector", SENSOR_TYPE_ROTATION_VECTOR,
         SENSOR_STRING_TYPE_ROTATION_VECTOR, Kind::kQuaternion, 1.0f, 1.0f, 1.0f / 65536, 3.85f,
         true},
        {"lsm6db0_game_quat", "Game Rotation Vector", SENSOR_TYPE_GAME_ROTATION_VECTOR,
         SENSOR_STRING_TYPE_GAME_ROTATION_VECTOR, Kind::kQuaternion, 1.0f, 1.0f, 1.0f / 65536,
         3.25f, false},
        {"lsm6db0_geo_quat", "Geomagnetic Rotation Vector",
         SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR, SENSOR_STRING_TYPE_GEOMAGNETIC_ROTATION_VECTOR,
         Kind::kQuaternion, 1.0f, 1.0f, 1.0f / 65536, 0.85f, true},
        {"lsm6db0_gravity", "Gravity", SENSOR_TYPE_GRAVITY, SENSOR_STRING_TYPE_GRAVITY,
         Kind::kFloats, 1.0f, 2 * kGravity, 1.0f / 1024, 3.25f, false},
        {"lsm6db0_linear", "Linear Acceleration", SENSOR_TYPE_LINEAR_ACCELERATION,
         SENSOR_STRING_TYPE_LINEAR_ACCELERATION, Kind::kFloats, 1.0f, 2 * kGravity, 1.0f / 1024,
         3.25f, false},
        {"lsm6db0_euler", "Orientation", SENSOR_TYPE_ORIENTATION, SENSOR_STRING_TYPE_ORIENTATION,
         Kind::kFloats, 1.0f, 360.0f, 1.0f / 64, 3.85f, false},
};

/* The data channels of a record, in index order: all but the time and the
 * accuracy. */
std::vector<const IioChannel*> Axes(const IioDevice& device) {
    std::vector<const IioChannel*> axes;
    for (const auto& channel : device.channels()) {
        if (channel.name == "in_timestamp") continue;
        if (channel.name.find("accuracy") != std::string::npos) continue;
        if (channel.storageBytes == 0) continue;
        axes.push_back(&channel);
    }
    return axes;
}

const IioChannel* Accuracy(const IioDevice& device) {
    for (const auto& channel : device.channels())
        if (channel.name.find("accuracy") != std::string::npos) return &channel;
    return nullptr;
}

}  // namespace

SensorHub::SensorHub(EventSink* sink) : sink_(sink) {}

SensorHub::~SensorHub() {
    stop_ = true;
    if (wake_ >= 0) {
        uint64_t one = 1;
        write(wake_, &one, sizeof(one));
    }
    if (thread_.joinable()) thread_.join();
    if (epoll_ >= 0) close(epoll_);
    if (wake_ >= 0) close(wake_);
}

bool SensorHub::Start() {
    epoll_ = epoll_create1(EPOLL_CLOEXEC);
    wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (epoll_ < 0 || wake_ < 0) return false;

    epoll_event event = {};
    event.events = EPOLLIN;
    event.data.u32 = 0;
    epoll_ctl(epoll_, EPOLL_CTL_ADD, wake_, &event);

    for (const auto& spec : kSpecs) {
        auto device = std::make_unique<IioDevice>();
        if (!device->Open(spec.iio)) {
            LOG(WARNING) << "no " << spec.iio;
            continue;
        }

        Sensor sensor;
        sensor.spec = &spec;
        sensor_t& info = sensor.info;
        memset(&info, 0, sizeof(info));
        info.name = spec.name;
        info.vendor = "STMicroelectronics";
        info.version = 1;
        info.handle = static_cast<int>(sensors_.size()) + 1;
        info.type = spec.type;
        info.stringType = spec.stringType;
        info.power = spec.power;
        info.flags = SENSOR_FLAG_CONTINUOUS_MODE;

        auto axes = Axes(*device);
        if (spec.kind == Kind::kVector || spec.kind == Kind::kUncalibrated) {
            /* An uncalibrated channel holds two halves of 16 bits. */
            float step = axes.empty() ? 1.0f : axes[0]->scale * spec.unit;
            info.resolution = step;
            info.maxRange = step * 32767;
        } else {
            info.resolution = spec.resolution;
            info.maxRange = spec.maxRange;
        }

        const auto& rates = device->rates();
        if (!rates.empty()) {
            info.minDelay = 1000000 / rates.back();
            info.maxDelay = 1000000 / rates.front();
        }

        sensor.device = std::move(device);
        epoll_event ready = {};
        ready.events = EPOLLIN;
        ready.data.u32 = static_cast<uint32_t>(info.handle);
        epoll_ctl(epoll_, EPOLL_CTL_ADD, sensor.device->fd(), &ready);
        sensors_.push_back(std::move(sensor));
    }

    for (const auto& sensor : sensors_) list_.push_back(sensor.info);
    if (sensors_.empty()) return false;

    thread_ = std::thread(&SensorHub::Loop, this);
    return true;
}

SensorHub::Sensor* SensorHub::Find(int handle) {
    if (handle < 1 || handle > static_cast<int>(sensors_.size())) return nullptr;
    return &sensors_[handle - 1];
}

int SensorHub::Activate(int handle, bool enabled) {
    std::lock_guard<std::mutex> guard(lock_);
    Sensor* sensor = Find(handle);
    if (!sensor) return -EINVAL;
    if (sensor->active == enabled) return 0;

    if (enabled && sensor->periodNs > 0) sensor->device->SetRate(1000000000 / sensor->periodNs);
    if (!sensor->device->SetEnabled(enabled)) return -EIO;
    sensor->active = enabled;
    return 0;
}

int SensorHub::Batch(int handle, int64_t periodNs, int64_t /* latencyNs */) {
    std::lock_guard<std::mutex> guard(lock_);
    Sensor* sensor = Find(handle);
    if (!sensor) return -EINVAL;

    /* Clamped to what the sensor can do, as Android allows. */
    int64_t fastest = int64_t{sensor->info.minDelay} * 1000;
    int64_t slowest = int64_t{sensor->info.maxDelay} * 1000;
    if (periodNs < fastest) periodNs = fastest;
    if (slowest > 0 && periodNs > slowest) periodNs = slowest;
    sensor->periodNs = periodNs;

    /* The rate at or above the asked one: a sensor may report faster than
     * asked, never slower. */
    int hz = static_cast<int>((1000000000 + periodNs - 1) / periodNs);
    if (sensor->active && !sensor->device->SetRate(hz)) return -EIO;
    return 0;
}

int SensorHub::Flush(int handle) {
    sensors_event_t event;
    {
        std::lock_guard<std::mutex> guard(lock_);
        Sensor* sensor = Find(handle);
        if (!sensor || !sensor->active) return -EINVAL;
    }

    /* Nothing is held back yet, so all there is has been delivered. */
    memset(&event, 0, sizeof(event));
    event.version = META_DATA_VERSION;
    event.type = SENSOR_TYPE_META_DATA;
    event.meta_data.what = META_DATA_FLUSH_COMPLETE;
    event.meta_data.sensor = handle;
    sink_->OnEvents({event});
    return 0;
}

void SensorHub::DeactivateAll() {
    for (const auto& sensor : list_) Activate(sensor.handle, false);
}

void SensorHub::Loop() {
    constexpr size_t kRecords = 16;
    std::vector<uint8_t> buffer;
    std::vector<sensors_event_t> events;
    epoll_event ready[8];

    while (!stop_) {
        int count = epoll_wait(epoll_, ready, 8, -1);
        if (count < 0) {
            if (errno == EINTR) continue;
            PLOG(ERROR) << "epoll_wait";
            return;
        }

        for (int i = 0; i < count; i++) {
            Sensor* sensor = Find(static_cast<int>(ready[i].data.u32));
            if (!sensor) continue;

            IioDevice& device = *sensor->device;
            buffer.resize(device.recordBytes() * kRecords);
            ssize_t got = device.Read(buffer.data(), buffer.size());
            if (got <= 0) continue;

            events.clear();
            for (ssize_t at = 0; at < got; at += device.recordBytes()) {
                sensors_event_t event;
                Convert(*sensor, buffer.data() + at, &event);
                /* The hub's fusion has been seen to put out NaN; nothing
                 * downstream takes it. */
                bool valid = true;
                for (int v = 0; v < 6; v++)
                    if (std::isnan(event.data[v])) valid = false;
                if (valid) events.push_back(event);
            }
            sensor->events += events.size();
            if (!events.empty()) sink_->OnEvents(events);
        }
    }
}

void SensorHub::Convert(const Sensor& sensor, const uint8_t* record,
                        sensors_event_t* event) const {
    const IioDevice& device = *sensor.device;
    const Spec& spec = *sensor.spec;

    memset(event, 0, sizeof(*event));
    event->version = sizeof(*event);
    event->sensor = sensor.info.handle;
    event->type = spec.type;
    if (const IioChannel* time = device.Channel("in_timestamp"))
        event->timestamp = IioDevice::Raw(*time, record);

    auto axes = Axes(device);
    const IioChannel* accuracy = Accuracy(device);
    int8_t status = accuracy ? static_cast<int8_t>(IioDevice::Raw(*accuracy, record))
                             : SENSOR_STATUS_ACCURACY_HIGH;

    switch (spec.kind) {
        case Kind::kVector:
            for (size_t i = 0; i < 3 && i < axes.size(); i++)
                event->data[i] = IioDevice::Raw(*axes[i], record) * axes[i]->scale * spec.unit;
            event->acceleration.status = status;
            break;

        case Kind::kUncalibrated:
            for (size_t i = 0; i < 3 && i < axes.size(); i++) {
                int64_t both = IioDevice::Raw(*axes[i], record);
                float step = axes[i]->scale * spec.unit;
                event->uncalibrated_gyro.uncalib[i] = static_cast<int16_t>(both & 0xffff) * step;
                event->uncalibrated_gyro.bias[i] = static_cast<int16_t>((both >> 16) & 0xffff) * step;
            }
            break;

        case Kind::kQuaternion: {
            float q[4] = {};
            for (size_t i = 0; i < 4 && i < axes.size(); i++) q[i] = IioDevice::Float(*axes[i], record);
            /* -q is the same rotation; Android's readers take w >= 0. */
            float sign = q[3] < 0 ? -1.0f : 1.0f;
            for (int i = 0; i < 4; i++) event->data[i] = sign * q[i];
            /* No estimate of the heading's accuracy is given. */
            if (spec.heading) event->data[4] = -1.0f;
            break;
        }

        case Kind::kFloats:
            for (size_t i = 0; i < 3 && i < axes.size(); i++)
                event->data[i] = IioDevice::Float(*axes[i], record);
            event->orientation.status = status;
            break;
    }
}

std::string SensorHub::Dump() {
    std::lock_guard<std::mutex> guard(lock_);
    std::string out;
    for (const auto& sensor : sensors_) {
        android::base::StringAppendF(&out, "%2d %-28s %-18s %s %3d Hz  %llu events\n",
                                     sensor.info.handle, sensor.info.name, sensor.spec->iio,
                                     sensor.active ? "on " : "off", sensor.device->rate(),
                                     static_cast<unsigned long long>(sensor.events));
    }
    return out;
}

}  // namespace sensors
}  // namespace mocha
