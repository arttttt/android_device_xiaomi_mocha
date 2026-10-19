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

#include "SubHal.h"

#include <errno.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <convertV2_1.h>
#include <sensors/convert.h>

namespace mocha {
namespace sensors {

using ::android::hardware::Void;
using ::android::hardware::sensors::V1_0::implementation::convertFromSensor;
using ::android::hardware::sensors::V1_0::implementation::convertFromSensorEvent;
using ::android::hardware::sensors::V2_1::implementation::convertToNewEvent;
using ::android::hardware::sensors::V2_1::implementation::convertToNewSensorInfo;

namespace {

Result ToResult(int err) {
    switch (err) {
        case 0:
            return Result::OK;
        case -EINVAL:
            return Result::BAD_VALUE;
        default:
            return Result::INVALID_OPERATION;
    }
}

}  // namespace

SubHal::SubHal() : hub_(this) {
    if (!hub_.Start()) LOG(ERROR) << "the sensor hub has no sensors";
}

Return<void> SubHal::getSensorsList_2_1(getSensorsList_2_1_cb callback) {
    std::vector<V2_1::SensorInfo> list;
    for (const auto& sensor : hub_.sensors()) {
        ::android::hardware::sensors::V1_0::SensorInfo info;
        convertFromSensor(sensor, &info);
        list.push_back(convertToNewSensorInfo(info));
    }
    callback(list);
    return Void();
}

/* Data injection is for testing fusion on a device that does its own; the
 * hub's fusion takes no input but its chips. */
Return<Result> SubHal::setOperationMode(OperationMode mode) {
    return mode == OperationMode::NORMAL ? Result::OK : Result::BAD_VALUE;
}

Return<Result> SubHal::activate(int32_t handle, bool enabled) {
    return ToResult(hub_.Activate(handle, enabled));
}

Return<Result> SubHal::batch(int32_t handle, int64_t periodNs, int64_t latencyNs) {
    return ToResult(hub_.Batch(handle, periodNs, latencyNs));
}

Return<Result> SubHal::flush(int32_t handle) {
    return ToResult(hub_.Flush(handle));
}

Return<Result> SubHal::injectSensorData_2_1(const V2_1::Event& /* event */) {
    return Result::INVALID_OPERATION;
}

Return<void> SubHal::registerDirectChannel(const SharedMemInfo& /* mem */,
                                           registerDirectChannel_cb callback) {
    callback(Result::INVALID_OPERATION, -1);
    return Void();
}

Return<Result> SubHal::unregisterDirectChannel(int32_t /* channelHandle */) {
    return Result::INVALID_OPERATION;
}

Return<void> SubHal::configDirectReport(int32_t /* handle */, int32_t /* channelHandle */,
                                        RateLevel /* rate */, configDirectReport_cb callback) {
    callback(Result::INVALID_OPERATION, 0);
    return Void();
}

Return<void> SubHal::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& /* args */) {
    if (fd.getNativeHandle() == nullptr || fd->numFds < 1) return Void();
    std::string dump = hub_.Dump();
    write(fd->data[0], dump.c_str(), dump.size());
    return Void();
}

/*
 * Called again whenever the framework restarts: what it had on is not on
 * for it any more.
 */
Return<Result> SubHal::initialize(const sp<IHalProxyCallback>& proxy) {
    hub_.DeactivateAll();
    std::lock_guard<std::mutex> guard(lock_);
    proxy_ = proxy;
    return Result::OK;
}

void SubHal::OnEvents(const std::vector<sensors_event_t>& events) {
    std::vector<V2_1::Event> converted;
    converted.reserve(events.size());
    for (const auto& event : events) {
        ::android::hardware::sensors::V1_0::Event old;
        convertFromSensorEvent(event, &old);
        converted.push_back(convertToNewEvent(old));
    }

    sp<IHalProxyCallback> proxy;
    {
        std::lock_guard<std::mutex> guard(lock_);
        proxy = proxy_;
    }
    /* None of the hub's sensors wakes the system, so the lock stays off. */
    if (proxy != nullptr) proxy->postEvents(converted, proxy->createScopedWakelock(false));
}

}  // namespace sensors
}  // namespace mocha

::android::hardware::sensors::V2_1::implementation::ISensorsSubHal* sensorsHalGetSubHal_2_1(
        uint32_t* version) {
    static mocha::sensors::SubHal subHal;
    *version = SUB_HAL_2_1_VERSION;
    return &subHal;
}
