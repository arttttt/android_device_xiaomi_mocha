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

#define LOG_TAG "android.hardware.power@1.3-service.mocha"

#include "Profiles.h"

#include <cutils/properties.h>

#include <string>

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

static const char PROPERTY[] = "sys.perf.profile";

int Profiles::current() const {
    return property_get_int32(PROPERTY, BALANCED);
}

void Profiles::set(int profile) const {
    property_set(PROPERTY, std::to_string(profile).c_str());
}

void Profiles::enterLowPower() {
    std::lock_guard<std::mutex> lock(mLock);

    if (mBeforeLowPower < 0) mBeforeLowPower = current();

    set(POWER_SAVE);
}

void Profiles::leaveLowPower() {
    std::lock_guard<std::mutex> lock(mLock);

    if (mBeforeLowPower < 0) return;

    set(mBeforeLowPower);
    mBeforeLowPower = -1;
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android
