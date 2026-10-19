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

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

namespace mocha {
namespace sensors {

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

    epoll_event ready = {};
    ready.events = EPOLLIN;
    ready.data.fd = wake_;
    epoll_ctl(epoll_, EPOLL_CTL_ADD, wake_, &ready);

    sensors_ = MakeSensors();
    for (const auto& sensor : sensors_) {
        list_.push_back(sensor->info());
        for (int fd : sensor->Fds()) {
            ready.data.fd = fd;
            epoll_ctl(epoll_, EPOLL_CTL_ADD, fd, &ready);
            owners_[fd] = sensor.get();
        }
    }
    if (sensors_.empty()) return false;

    thread_ = std::thread(&SensorHub::Loop, this);
    return true;
}

Sensor* SensorHub::Find(int handle) {
    if (handle < 1 || handle > static_cast<int>(sensors_.size())) return nullptr;
    return sensors_[handle - 1].get();
}

int SensorHub::Activate(int handle, bool enabled) {
    std::lock_guard<std::mutex> guard(lock_);
    Sensor* sensor = Find(handle);
    return sensor ? sensor->Activate(enabled) : -EINVAL;
}

int SensorHub::Batch(int handle, int64_t periodNs, int64_t /* latencyNs */) {
    std::lock_guard<std::mutex> guard(lock_);
    Sensor* sensor = Find(handle);
    return sensor ? sensor->SetPeriod(periodNs) : -EINVAL;
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
        int count = epoll_wait(epoll_, ready, 8, -1);
        if (count < 0) {
            if (errno == EINTR) continue;
            PLOG(ERROR) << "epoll_wait";
            return;
        }

        events.clear();
        {
            std::lock_guard<std::mutex> guard(lock_);
            for (int i = 0; i < count; i++) {
                auto owner = owners_.find(ready[i].data.fd);
                if (owner != owners_.end()) owner->second->OnReadable(ready[i].data.fd, &events);
            }
        }
        if (!events.empty()) sink_->OnEvents(events);
    }
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
