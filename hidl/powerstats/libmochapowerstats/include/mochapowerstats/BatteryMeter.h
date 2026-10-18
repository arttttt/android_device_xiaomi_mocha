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
#ifndef MOCHA_POWERSTATS_BATTERY_METER_H
#define MOCHA_POWERSTATS_BATTERY_METER_H

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace mocha {
namespace powerstats {

struct Energy {
    /* CLOCK_BOOTTIME of the sample the total was brought up to. */
    int64_t timestampMs;
    /* Since the meter started. */
    int64_t durationMs;
    /* Drawn from the battery since the meter started. */
    int64_t energyUWs;
};

/*
 * Energy drawn from the battery, out of the INA231 on its line (VDD_BAT).
 *
 * The chip and its driver give instantaneous readings only, so the meter
 * keeps the integral: a sample a second while the system is awake, the
 * trapezoid between two samples. It cannot sample a suspended system, so a
 * suspend is bridged by the fuel gauge instead -- the charge it lost over the
 * window, less what the samples already account for.
 */
class BatteryMeter {
  public:
    static constexpr const char* kRail = "VDD_BAT";

    ~BatteryMeter();

    /* Finds the INA by its rail name and starts sampling; false if the
     * board has no such rail. */
    bool Start();
    /* Takes a sample first, so the total is as of now. */
    Energy Read();

  private:
    struct Clock {
        int64_t bootMs;
        int64_t monoMs;
    };

    static Clock Now();
    void Loop();
    bool ReadPowerMw(int64_t* mw) const;
    void SampleLocked();
    void AddLocked(int64_t uws);
    void ResetWindowLocked(int64_t chargeUAh);
    void BridgeSuspendLocked();

    std::string ui_;
    std::mutex lock_;
    std::condition_variable wake_;
    std::thread thread_;
    bool stop_ = false;

    Clock start_{};
    Clock last_{};
    int64_t lastMw_ = 0;
    /* What the samples add up to, and what is reported: the samples and the
     * suspends. */
    int64_t awakeUWs_ = 0;
    int64_t totalUWs_ = 0;
    /* An interval that put energy back into the battery, owed against the
     * next ones (see AddLocked). */
    int64_t carryUWs_ = 0;

    /* The window over which suspends are reconciled with the gauge, opened
     * at the gauge reading it starts from. */
    int64_t windowChargeUAh_ = -1;
    int64_t windowAwakeUWs_ = 0;
    int64_t windowAwakeMs_ = 0;
    int64_t windowSuspendMs_ = 0;
};

}  // namespace powerstats
}  // namespace mocha

#endif  // MOCHA_POWERSTATS_BATTERY_METER_H
