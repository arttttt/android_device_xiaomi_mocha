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
#include <sys/inotify.h>
#include <time.h>
#include <unistd.h>

#include <cstring>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

namespace mocha {
namespace sensors {

namespace {

/* What the hub has learnt is kept this often while sensors run. */
constexpr int64_t kSaveEveryNs = 5 * 60 * 1000000000LL;
/* ...and not this soon after the fusion has failed. */
constexpr int64_t kNoSaveAfterFaultNs = 60LL * 1000000000;

int64_t BootNs() {
    timespec now;
    clock_gettime(CLOCK_BOOTTIME, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
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
    if (inotify_ >= 0) close(inotify_);
}

bool SensorHub::Start() {
    epoll_ = epoll_create1(EPOLL_CLOEXEC);
    wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (epoll_ < 0 || wake_ < 0) return false;

    epoll_event ready = {};
    ready.events = EPOLLIN;
    ready.data.fd = wake_;
    epoll_ctl(epoll_, EPOLL_CTL_ADD, wake_, &ready);

    /* What the hub lost on this boot goes back first. */
    haveHub_ = calibration_.Open();
    if (haveHub_) calibration_.Restore();

    /* A new calibration from the settings is taken as soon as it is
     * written, and a removed one dropped as soon as it is gone. */
    inotify_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (inotify_ >= 0 && inotify_add_watch(inotify_, HubCalibration::kDirectory,
                                           IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE) >= 0) {
        ready.data.fd = inotify_;
        epoll_ctl(epoll_, EPOLL_CTL_ADD, inotify_, &ready);
    } else {
        PLOG(WARNING) << "cannot watch " << HubCalibration::kDirectory;
    }

    sensors_ = MakeSensors(&calibration_);
    for (const auto& sensor : sensors_) {
        list_.push_back(sensor->info());
        for (int fd : sensor->Fds()) {
            ready.data.fd = fd;
            epoll_ctl(epoll_, EPOLL_CTL_ADD, fd, &ready);
            owners_[fd] = sensor.get();
        }
    }
    if (sensors_.empty()) return false;

    /* What was just restored need not be kept again at once. */
    lastSaveNs_ = BootNs();
    thread_ = std::thread(&SensorHub::Loop, this);
    return true;
}

Sensor* SensorHub::Find(int handle) {
    if (handle < 1 || handle > static_cast<int>(sensors_.size())) return nullptr;
    return sensors_[handle - 1].get();
}

int SensorHub::Activate(int handle, bool enabled) {
    std::vector<sensors_event_t> events;
    int err;
    {
        std::lock_guard<std::mutex> guard(lock_);
        Sensor* sensor = Find(handle);
        if (!sensor) return -EINVAL;
        bool was = sensor->active();
        err = sensor->Activate(enabled);
        if (!err && enabled && !was) sensor->OnActivated(&events);

        /* The last one off: keep what the hub has learnt while it ran. */
        bool any = false;
        for (const auto& other : sensors_) any = any || other->active();
        if (!err && was && !enabled && !any) SaveCalibration();
    }
    if (!events.empty()) sink_->OnEvents(events);
    return err;
}

int SensorHub::Batch(int handle, int64_t periodNs, int64_t latencyNs) {
    std::lock_guard<std::mutex> guard(lock_);
    Sensor* sensor = Find(handle);
    return sensor ? sensor->Batch(periodNs, latencyNs) : -EINVAL;
}

int SensorHub::Flush(int handle) {
    std::vector<sensors_event_t> events;
    int err;
    {
        std::lock_guard<std::mutex> guard(lock_);
        Sensor* sensor = Find(handle);
        err = sensor ? sensor->Flush(&events) : -EINVAL;
    }
    if (!events.empty()) sink_->OnEvents(events);
    return err;
}

void SensorHub::DeactivateAll() {
    for (const auto& sensor : list_) Activate(sensor.handle, false);
}

void SensorHub::Loop() {
    std::vector<sensors_event_t> events;
    epoll_event ready[8];

    while (!stop_) {
        /* Sensors that run wake this up many times a second, so the time to
         * keep the calibration comes by the clock, not by an idle wait. */
        int64_t untilSave = lastSaveNs_ + kSaveEveryNs - BootNs();
        int count = epoll_wait(epoll_, ready, 8,
                               untilSave > 0 ? static_cast<int>(untilSave / 1000000) + 1 : 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            PLOG(ERROR) << "epoll_wait";
            return;
        }

        events.clear();
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (BootNs() - lastSaveNs_ >= kSaveEveryNs) SaveCalibration();
            for (int i = 0; i < count; i++) {
                int fd = ready[i].data.fd;
                if (fd == inotify_) {
                    OnCalibrationFile();
                    continue;
                }
                auto owner = owners_.find(fd);
                if (owner != owners_.end()) owner->second->OnReadable(fd, &events);
            }

            bool fault = false;
            for (const auto& sensor : sensors_) fault = sensor->TakeFusionFault() || fault;
            if (fault) ResetFusion();
        }
        if (!events.empty()) sink_->OnEvents(events);
    }
}

void SensorHub::OnCalibrationFile() {
    alignas(inotify_event) char buffer[512];
    bool accel = false;
    ssize_t got;
    while ((got = read(inotify_, buffer, sizeof(buffer))) > 0) {
        for (ssize_t at = 0; at < got;) {
            auto* event = reinterpret_cast<inotify_event*>(buffer + at);
            if (event->len > 0 && strcmp(event->name, "accel.cal") == 0) accel = true;
            at += sizeof(inotify_event) + event->len;
        }
    }
    if (!accel || !haveHub_) return;

    LOG(INFO) << "the accelerometer's calibration changed";
    calibration_.ReloadAccel();
    for (const auto& sensor : sensors_) sensor->OnCalibrationChanged();
}

/*
 * Found by trial: the fusion recovers from a restart of the magnetometer
 * calibration only if its own outputs are off while it happens.
 */
void SensorHub::ResetFusion() {
    if (!haveHub_) return;
    LOG(WARNING) << "the nine-axis fusion puts out NaN; restarting the magnetometer calibration";
    lastFaultNs_ = BootNs();
    for (const auto& sensor : sensors_)
        if (sensor->UsesNineAxisFusion()) sensor->Pause(true);
    calibration_.ResetMagnetometer();
    for (const auto& sensor : sensors_)
        if (sensor->UsesNineAxisFusion()) sensor->Pause(false);
}

void SensorHub::SaveCalibration() {
    lastSaveNs_ = BootNs();
    if (!haveHub_) return;
    if (lastFaultNs_ > 0 && BootNs() - lastFaultNs_ < kNoSaveAfterFaultNs) return;
    calibration_.Save();
}

std::string SensorHub::Dump() {
    std::lock_guard<std::mutex> guard(lock_);
    std::string out;
    for (const auto& sensor : sensors_) {
        android::base::StringAppendF(&out, "%2d %-28s %s  %llu events\n", sensor->info().handle,
                                     sensor->info().name, sensor->Describe().c_str(),
                                     static_cast<unsigned long long>(sensor->events()));
    }
    return out;
}

}  // namespace sensors
}  // namespace mocha
