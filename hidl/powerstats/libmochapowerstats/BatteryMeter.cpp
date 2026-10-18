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

#define LOG_TAG "mochapowerstats"

#include <mochapowerstats/BatteryMeter.h>

#include <dirent.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <memory>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace mocha {
namespace powerstats {

namespace {

constexpr const char* kIioDevices = "/sys/bus/iio/devices";
constexpr const char* kInaName = "ina230x";
constexpr const char* kGaugeCharge = "/sys/class/power_supply/battery/charge_now";
constexpr const char* kGaugeVoltage = "/sys/class/power_supply/battery/voltage_now";

constexpr auto kPeriod = std::chrono::seconds(1);

/* A gap between two samples longer than this, in BOOTTIME over MONOTONIC,
 * was a suspend and not a late wake-up. */
constexpr int64_t kSuspendMs = 500;

/* How much energy going back into the battery is held against the next
 * intervals. The samples are of a spiky load, and on external power the
 * battery swings either way around nothing; held against each other the
 * swings cancel instead of the positive half adding up. Two joules is a few
 * seconds of such swings, and small enough that a long charge cannot eat the
 * discharge after it. */
constexpr int64_t kCarryLimitUWs = 2'000'000;

/* No suspend draws more than this; a reconciliation that says otherwise is
 * the samples and the gauge disagreeing about the awake part, and is cut. */
constexpr int64_t kSuspendMaxMw = 200;

bool ReadInt(const std::string& path, int64_t* value) {
    std::string s;
    if (!android::base::ReadFileToString(path, &s)) return false;
    return sscanf(s.c_str(), "%" SCNd64, value) == 1;
}

std::string ReadLine(const std::string& path) {
    std::string s;
    android::base::ReadFileToString(path, &s);
    return android::base::Trim(s);
}

}  // namespace

BatteryMeter::~BatteryMeter() {
    {
        std::lock_guard<std::mutex> lock(lock_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

BatteryMeter::Clock BatteryMeter::Now() {
    timespec boot, mono;
    clock_gettime(CLOCK_BOOTTIME, &boot);
    clock_gettime(CLOCK_MONOTONIC, &mono);
    return {boot.tv_sec * 1000 + boot.tv_nsec / 1000000,
            mono.tv_sec * 1000 + mono.tv_nsec / 1000000};
}

bool BatteryMeter::Start() {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kIioDevices), closedir);
    if (!dir) {
        PLOG(ERROR) << "cannot list " << kIioDevices;
        return false;
    }
    while (dirent* e = readdir(dir.get())) {
        const std::string dev = std::string(kIioDevices) + "/" + e->d_name;
        if (ReadLine(dev + "/name") == kInaName && ReadLine(dev + "/rail_name") == kRail) {
            ui_ = dev + "/ui_input";
            break;
        }
    }
    if (ui_.empty()) {
        LOG(ERROR) << "no " << kInaName << " on " << kRail;
        return false;
    }

    std::lock_guard<std::mutex> lock(lock_);
    start_ = last_ = Now();
    ReadPowerMw(&lastMw_);
    int64_t charge;
    ResetWindowLocked(ReadInt(kGaugeCharge, &charge) ? charge : -1);
    thread_ = std::thread(&BatteryMeter::Loop, this);
    LOG(INFO) << kRail << " from " << ui_;
    return true;
}

Energy BatteryMeter::Read() {
    std::lock_guard<std::mutex> lock(lock_);
    SampleLocked();
    return {last_.bootMs, last_.bootMs - start_.bootMs, totalUWs_};
}

void BatteryMeter::Loop() {
    std::unique_lock<std::mutex> lock(lock_);
    while (!wake_.wait_for(lock, kPeriod, [this] { return stop_; })) SampleLocked();
}

/*
 * ui_input is one conversion, bus voltage and signed current together --
 * in_power_input is the chip's power register, which has no sign, and the
 * separate voltage and current files are a conversion each. The current is
 * negative while the battery discharges.
 */
bool BatteryMeter::ReadPowerMw(int64_t* mw) const {
    std::string s;
    int64_t mv, ma;
    if (!android::base::ReadFileToString(ui_, &s) ||
        sscanf(s.c_str(), "%" SCNd64 " %" SCNd64, &mv, &ma) != 2) {
        return false;
    }
    *mw = mv * -ma / 1000;
    return true;
}

void BatteryMeter::SampleLocked() {
    const Clock now = Now();
    int64_t mw = lastMw_;
    ReadPowerMw(&mw);

    const int64_t awakeMs = now.monoMs - last_.monoMs;
    const int64_t suspendMs = (now.bootMs - last_.bootMs) - awakeMs;
    /* mW by ms is µWs. */
    AddLocked((lastMw_ + mw) * awakeMs / 2);
    windowAwakeMs_ += awakeMs;
    if (suspendMs > 0) windowSuspendMs_ += suspendMs;

    last_ = now;
    lastMw_ = mw;

    if (suspendMs > kSuspendMs) BridgeSuspendLocked();
}

void BatteryMeter::AddLocked(int64_t uws) {
    const int64_t sum = carryUWs_ + uws;
    if (sum > 0) {
        awakeUWs_ += sum;
        totalUWs_ += sum;
        carryUWs_ = 0;
    } else {
        carryUWs_ = std::max(sum, -kCarryLimitUWs);
    }
}

void BatteryMeter::ResetWindowLocked(int64_t chargeUAh) {
    windowChargeUAh_ = chargeUAh;
    windowAwakeUWs_ = awakeUWs_;
    windowAwakeMs_ = 0;
    windowSuspendMs_ = 0;
}

/*
 * After a suspend: the gauge's lost charge over the window, less what the
 * samples counted in it, is what the suspends drew. The gauge is read only
 * here -- a read of it is a uevent and a redraw of the battery icon, not
 * something to do every second.
 *
 * Its step is a milliampere-hour, so a window stays open until it has moved.
 * A window the system spent mostly awake is closed without a word: there the
 * difference is the samples and the gauge disagreeing about the awake part,
 * not the suspends, which were the smaller share anyway.
 */
void BatteryMeter::BridgeSuspendLocked() {
    int64_t charge, uv;
    if (!ReadInt(kGaugeCharge, &charge) || !ReadInt(kGaugeVoltage, &uv)) return;

    if (windowChargeUAh_ < 0 || charge > windowChargeUAh_) {
        /* Started, or charged: nothing to reconcile. */
        ResetWindowLocked(charge);
        return;
    }
    if (charge == windowChargeUAh_) return;

    if (windowAwakeMs_ <= windowSuspendMs_) {
        /* µAh by mV is 3.6 µWs. */
        const int64_t gaugeUWs = (windowChargeUAh_ - charge) * (uv / 1000) * 36 / 10;
        const int64_t suspendUWs =
                std::clamp(gaugeUWs - (awakeUWs_ - windowAwakeUWs_), int64_t(0),
                           windowSuspendMs_ * kSuspendMaxMw);
        totalUWs_ += suspendUWs;
    }
    ResetWindowLocked(charge);
}

}  // namespace powerstats
}  // namespace mocha
