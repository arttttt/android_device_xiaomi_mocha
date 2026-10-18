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

#include "PowerStats.h"

namespace aidl {
namespace android {
namespace hardware {
namespace power {
namespace stats {

namespace {

/* The one channel, the battery line. */
constexpr int32_t kBatteryChannel = 0;

ndk::ScopedAStatus IllegalArgument() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

}  // namespace

PowerStats::PowerStats() {
    hasBattery_ = battery_.Start();
    residencies_.Init();
}

ndk::ScopedAStatus PowerStats::getPowerEntityInfo(std::vector<PowerEntity>* _aidl_return) {
    for (const auto& e : residencies_.entities()) {
        PowerEntity entity{.id = e.id, .name = e.name};
        for (const auto& s : e.states) entity.states.push_back({.id = s.id, .name = s.name});
        _aidl_return->push_back(std::move(entity));
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus PowerStats::getStateResidency(const std::vector<int32_t>& in_powerEntityIds,
                                                 std::vector<StateResidencyResult>* _aidl_return) {
    std::vector<int32_t> ids = in_powerEntityIds;
    if (ids.empty()) {
        for (const auto& e : residencies_.entities()) ids.push_back(e.id);
    }
    for (int32_t id : ids) {
        std::vector<mocha::powerstats::Residency> r;
        if (!residencies_.Read(id, &r)) return IllegalArgument();
        StateResidencyResult result{.id = id};
        for (const auto& s : r) {
            result.stateResidencyData.push_back({.id = s.stateId,
                                                 .totalTimeInStateMs = s.timeMs,
                                                 .totalStateEntryCount = s.entries,
                                                 .lastEntryTimestampMs = s.lastEntryMs});
        }
        _aidl_return->push_back(std::move(result));
    }
    return ndk::ScopedAStatus::ok();
}

/* Nothing on the board measures a subsystem apart from the rest; what the
 * battery line gives is the whole device. */
ndk::ScopedAStatus PowerStats::getEnergyConsumerInfo(std::vector<EnergyConsumer>*) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus PowerStats::getEnergyConsumed(const std::vector<int32_t>& in_energyConsumerIds,
                                                 std::vector<EnergyConsumerResult>*) {
    return in_energyConsumerIds.empty() ? ndk::ScopedAStatus::ok() : IllegalArgument();
}

ndk::ScopedAStatus PowerStats::getEnergyMeterInfo(std::vector<Channel>* _aidl_return) {
    if (hasBattery_) {
        _aidl_return->push_back({.id = kBatteryChannel,
                                 .name = mocha::powerstats::BatteryMeter::kRail,
                                 .subsystem = "Battery"});
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus PowerStats::readEnergyMeter(const std::vector<int32_t>& in_channelIds,
                                               std::vector<EnergyMeasurement>* _aidl_return) {
    for (int32_t id : in_channelIds) {
        if (!hasBattery_ || id != kBatteryChannel) return IllegalArgument();
    }
    if (!hasBattery_) return ndk::ScopedAStatus::ok();

    const mocha::powerstats::Energy e = battery_.Read();
    _aidl_return->push_back({.id = kBatteryChannel,
                             .timestampMs = e.timestampMs,
                             .durationMs = e.durationMs,
                             .energyUWs = e.energyUWs});
    return ndk::ScopedAStatus::ok();
}

}  // namespace stats
}  // namespace power
}  // namespace hardware
}  // namespace android
}  // namespace aidl
