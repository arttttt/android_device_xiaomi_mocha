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

#define LOG_TAG "android.hardware.power-service.mocha"

#include "Profiles.h"

#include <cutils/properties.h>

namespace aidl::android::hardware::power::impl::mocha {

static const char PROFILE[] = "sys.perf.profile";
static const char CHOSEN[] = "persist.vendor.mocha.perf.profile";
static const char LOW_POWER[] = "vendor.power.low_power";

int Profiles::current() const {
    return property_get_int32(PROFILE, BALANCED);
}

int Profiles::chosen() const {
    return property_get_int32(CHOSEN, BALANCED);
}

void Profiles::setLowPower(bool on) const {
    property_set(LOW_POWER, on ? "1" : "0");
}

}  // namespace aidl::android::hardware::power::impl::mocha
