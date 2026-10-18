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

#define LOG_TAG "vendor.arttttt.mochamanager-service"

#include "MochaManager.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>

namespace aidl {
namespace vendor {
namespace arttttt {
namespace mochamanager {

namespace {

using ::aidl::android::hardware::power::stats::Channel;
using ::aidl::android::hardware::power::stats::EnergyMeasurement;
using ::aidl::android::hardware::power::stats::IPowerStats;

constexpr const char* kPowerStats = "android.hardware.power.stats.IPowerStats/default";
/* The rail the power.stats HAL meters the battery as. */
constexpr const char* kBatteryRail = "VDD_BAT";

ndk::ScopedAStatus Failed(const char* what) {
    return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(0, what);
}

}  // namespace

MochaManager::MochaManager() {
    board_.Init();
}

ndk::ScopedAStatus MochaManager::getDeviceInfo(DeviceInfo* _aidl_return) {
    const auto d = board_.ReadDeviceInfo();
    _aidl_return->model = d.model;
    _aidl_return->build = d.build;
    _aidl_return->kernel = d.kernel;
    _aidl_return->memTotalKb = d.memTotalKb;
    _aidl_return->wifi = d.wifi;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus MochaManager::getBattery(BatteryInfo* _aidl_return) {
    const auto b = board_.ReadBattery();
    _aidl_return->capacityPct = b.capacityPct;
    _aidl_return->status = b.status;
    _aidl_return->health = b.health;
    _aidl_return->voltageUv = b.voltageUv;
    _aidl_return->currentUa = b.currentUa;
    _aidl_return->tempDeciC = b.tempDeciC;
    _aidl_return->chargeNowUah = b.chargeNowUah;
    _aidl_return->chargeFullUah = b.chargeFullUah;
    _aidl_return->chargeFullDesignUah = b.chargeFullDesignUah;
    _aidl_return->cycleCount = b.cycleCount;
    _aidl_return->usbOnline = b.usbOnline;
    _aidl_return->acOnline = b.acOnline;
    _aidl_return->chargingDisabled = b.chargingDisabled;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus MochaManager::readPower(PowerSample* _aidl_return) {
    mocha::manager::PowerSample s;
    if (!board_.ReadPower(&s)) return Failed("no INA on the battery line");
    _aidl_return->timestampMs = s.timestampMs;
    _aidl_return->voltageMv = s.voltageMv;
    _aidl_return->currentMa = s.currentMa;
    _aidl_return->powerMw = s.powerMw;
    _aidl_return->energyUWs = BatteryEnergyUWs();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus MochaManager::readSnapshot(Snapshot* _aidl_return) {
    const auto s = board_.ReadSnapshot();
    _aidl_return->cluster = s.cluster;
    for (const auto& c : s.cores) {
        _aidl_return->cores.push_back({.id = c.id,
                                       .online = c.online,
                                       .curKhz = c.curKhz,
                                       .minKhz = c.minKhz,
                                       .maxKhz = c.maxKhz,
                                       .governor = c.governor});
    }
    _aidl_return->gpu = {.curHz = s.gpu.curHz,
                         .maxHz = s.gpu.maxHz,
                         .loadPermille = s.gpu.loadPermille,
                         .railgated = s.gpu.railgated};
    _aidl_return->emcHz = s.emcHz;
    for (const auto& t : s.thermals) {
        _aidl_return->thermals.push_back({.name = t.name, .milliCelsius = t.milliCelsius});
    }
    const auto& m = s.memory;
    _aidl_return->memory = {.totalKb = m.totalKb,
                            .availableKb = m.availableKb,
                            .nvmapUsedKb = m.nvmapUsedKb,
                            .swapTotalKb = m.swapTotalKb,
                            .swapFreeKb = m.swapFreeKb,
                            .zramOrigBytes = m.zramOrigBytes,
                            .zramComprBytes = m.zramComprBytes,
                            .zramUsedBytes = m.zramUsedBytes,
                            .psiSome = m.psiSome,
                            .psiFull = m.psiFull};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus MochaManager::setChargingDisabled(bool in_disabled) {
    return board_.SetChargingDisabled(in_disabled) ? ndk::ScopedAStatus::ok()
                                                   : Failed("cannot set the charger property");
}

ndk::ScopedAStatus MochaManager::getPerformanceProfile(int32_t* _aidl_return) {
    *_aidl_return = board_.PerformanceProfile();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus MochaManager::getActivePerformanceProfile(int32_t* _aidl_return) {
    *_aidl_return = board_.ActivePerformanceProfile();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus MochaManager::setPerformanceProfile(int32_t in_profile) {
    if (in_profile < PROFILE_POWER_SAVE || in_profile > PROFILE_EFFICIENCY) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    return board_.SetPerformanceProfile(in_profile) ? ndk::ScopedAStatus::ok()
                                                    : Failed("cannot set the profile property");
}

/*
 * Asked, not waited for: a missing HAL costs the energy field, not the
 * reading it rides along with. A HAL that went away is asked for again.
 */
int64_t MochaManager::BatteryEnergyUWs() {
    std::lock_guard<std::mutex> lock(powerStatsLock_);
    if (!powerStats_) {
        powerStats_ = IPowerStats::fromBinder(ndk::SpAIBinder(AServiceManager_checkService(kPowerStats)));
        if (!powerStats_) return -1;
    }

    std::vector<Channel> channels;
    std::vector<EnergyMeasurement> energy;
    if (!powerStats_->getEnergyMeterInfo(&channels).isOk()) {
        powerStats_.reset();
        return -1;
    }
    for (const auto& c : channels) {
        if (c.name != kBatteryRail) continue;
        if (!powerStats_->readEnergyMeter({c.id}, &energy).isOk() || energy.empty()) return -1;
        return energy[0].energyUWs;
    }
    return -1;
}

}  // namespace mochamanager
}  // namespace arttttt
}  // namespace vendor
}  // namespace aidl
