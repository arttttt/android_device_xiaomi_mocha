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

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

/*
 * How hard this device should be trying.
 *
 * The profile in force is sys.perf.profile, and power.mocha.rc is its only
 * writer: it reacts to the value by writing the governor's knobs, and it
 * decides the value from two inputs -- what the user chose in the tile
 * (persist.sys.perf.profile) and whether the battery saver is on. This
 * reports the second input and reads the result.
 *
 * The battery saver is expressed as the power save profile -- one notion of
 * how hard to try rather than two that can disagree -- and init, not this,
 * remembers the user's choice across it: the choice lives in a persistent
 * property, so a restarted HAL or a reboot in the middle of the saver does
 * not lose it.
 */
class Profiles {
  public:
    static constexpr int POWER_SAVE = 0;
    static constexpr int BALANCED = 1;

    int current() const;

    bool isPowerSave() const { return current() == POWER_SAVE; }

    /* The battery saver arriving and leaving. */
    void setLowPower(bool on) const;
};

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_POWER_PROFILES_H
