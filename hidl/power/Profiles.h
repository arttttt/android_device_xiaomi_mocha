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
#ifndef MOCHA_POWER_PROFILES_H
#define MOCHA_POWER_PROFILES_H

#include <mutex>

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

/*
 * How hard this device should be trying, as the user chose it.
 *
 * The profile is a property rather than anything of ours: init reacts to it
 * and writes the governor's knobs, and the tile in the interface writes the
 * property. This only reads it, sets it, and remembers what it replaced.
 *
 * Remembering matters because the battery saver is expressed as a profile too
 * -- one notion of how hard to try rather than two that can disagree -- and
 * the framework says only "low power on" and "low power off". It does not
 * carry what was chosen before, so without this the user's choice would be
 * replaced for good by turning the saver on once.
 */
class Profiles {
  public:
    static constexpr int POWER_SAVE = 0;
    static constexpr int BALANCED = 1;
    static constexpr int COUNT = 4;

    int current() const;
    void set(int profile) const;

    bool isPowerSave() const { return current() == POWER_SAVE; }

    /* The battery saver arriving and leaving. Entering twice does not forget
     * what was chosen before the first time. */
    void enterLowPower();
    void leaveLowPower();

  private:
    std::mutex mLock;

    /* What was in force before the saver took over, or below zero when it is
     * not in force. */
    int mBeforeLowPower = -1;
};

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_POWER_PROFILES_H
