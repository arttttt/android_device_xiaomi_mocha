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

#define LOG_TAG "android.hardware.light@2.0-service.mocha"

#include "Backlight.h"

#include <SysfsNode.h>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

/* What the framework counts in, and what it takes a missing ceiling to mean. */
static constexpr uint32_t FRAMEWORK_MAX = 255;

Backlight::Backlight(std::string brightnessPath, const std::string& maxBrightnessPath)
    : mPath(std::move(brightnessPath)),
      mMax(mocha::sysfs::read(maxBrightnessPath, FRAMEWORK_MAX)) {}

void Backlight::set(uint32_t brightness) const {
    if (mMax != FRAMEWORK_MAX) {
        brightness = brightness * mMax / FRAMEWORK_MAX;
    }

    mocha::sysfs::write(mPath, static_cast<int>(brightness));
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
