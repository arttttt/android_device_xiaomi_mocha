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
#ifndef MOCHA_MANAGER_SERVICE_H
#define MOCHA_MANAGER_SERVICE_H

#include <mutex>

#include <aidl/android/hardware/power/stats/IPowerStats.h>
#include <aidl/vendor/arttttt/mochamanager/BnMochaManager.h>

#include <mochamanager/Board.h>

namespace aidl {
namespace vendor {
namespace arttttt {
namespace mochamanager {

class MochaManager : public BnMochaManager {
  public:
    MochaManager();

    ndk::ScopedAStatus getDeviceInfo(DeviceInfo* _aidl_return) override;
    ndk::ScopedAStatus getBattery(BatteryInfo* _aidl_return) override;
    ndk::ScopedAStatus readPower(PowerSample* _aidl_return) override;
    ndk::ScopedAStatus readSnapshot(Snapshot* _aidl_return) override;
    ndk::ScopedAStatus setChargingDisabled(bool in_disabled) override;
    ndk::ScopedAStatus getPerformanceProfile(int32_t* _aidl_return) override;
    ndk::ScopedAStatus getActivePerformanceProfile(int32_t* _aidl_return) override;
    ndk::ScopedAStatus setPerformanceProfile(int32_t in_profile) override;
    ndk::ScopedAStatus getAccelCalibration(AccelCalibration* _aidl_return) override;
    ndk::ScopedAStatus setAccelCalibration(const AccelCalibration& in_calibration) override;
    ndk::ScopedAStatus resetAccelCalibration() override;

  private:
    /* The battery's energy since boot from the power.stats HAL, which already
     * integrates the INA; -1 when it does not answer. */
    int64_t BatteryEnergyUWs();

    mocha::manager::Board board_;
    std::mutex powerStatsLock_;
    std::shared_ptr<::aidl::android::hardware::power::stats::IPowerStats> powerStats_;
};

}  // namespace mochamanager
}  // namespace arttttt
}  // namespace vendor
}  // namespace aidl

#endif  // MOCHA_MANAGER_SERVICE_H
