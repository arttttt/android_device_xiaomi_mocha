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
#ifndef MOCHA_SENSORS_SUBHAL_H
#define MOCHA_SENSORS_SUBHAL_H

#include <V2_1/SubHal.h>

#include <mutex>

#include <mochasensors/SensorHub.h>

namespace mocha {
namespace sensors {

using ::android::sp;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SharedMemInfo;
using ::android::hardware::sensors::V2_1::implementation::IHalProxyCallback;
using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;
namespace V2_0 = ::android::hardware::sensors::V2_0;
namespace V2_1 = ::android::hardware::sensors::V2_1;

/*
 * The hub's sensors as a sub-HAL of AOSP's multi-HAL 2.1, which owns the
 * queues to the framework, the wake lock and the interface itself. This
 * only translates between ISensors and libmochasensors; the Android 13
 * AIDL multi-HAL hosts 2.1 sub-HALs the same way.
 */
class SubHal : public ISensorsSubHal, public EventSink {
  public:
    SubHal();

    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb callback) override;
    Return<Result> setOperationMode(OperationMode mode) override;
    Return<Result> activate(int32_t handle, bool enabled) override;
    Return<Result> batch(int32_t handle, int64_t periodNs, int64_t latencyNs) override;
    Return<Result> flush(int32_t handle) override;
    Return<Result> injectSensorData_2_1(const V2_1::Event& event) override;
    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb callback) override;
    Return<Result> unregisterDirectChannel(int32_t channelHandle) override;
    Return<void> configDirectReport(int32_t handle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb callback) override;
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;

    const std::string getName() override { return "mocha-sensorhub"; }
    Return<Result> initialize(const sp<IHalProxyCallback>& proxy) override;

    void OnEvents(const std::vector<sensors_event_t>& events) override;

  private:
    SensorHub hub_;
    std::mutex lock_;
    sp<IHalProxyCallback> proxy_;
};

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_SUBHAL_H
