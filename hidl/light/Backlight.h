/*
 * Copyright (C) 2026 The LineageOS Project
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
#ifndef MOCHA_LIGHT_BACKLIGHT_H
#define MOCHA_LIGHT_BACKLIGHT_H

#include <cstdint>
#include <string>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

/*
 * A lamp that is only ever a brightness: the panel behind the screen, and the
 * one behind the capacitive keys.
 *
 * One class for both because they are the same thing at different nodes with
 * different ceilings -- which was two nearly identical methods before, and
 * would have been three the day something else on this board lights up.
 *
 * The ceiling is read from the driver rather than assumed, and the framework's
 * 0..255 is scaled onto it. A panel that accepts 0..4095 and is handed 255
 * sits at a sixteenth of the brightness that was asked for.
 */
class Backlight {
  public:
    Backlight(std::string brightnessPath, const std::string& maxBrightnessPath);

    void set(uint32_t brightness) const;

  private:
    const std::string mPath;
    const uint32_t mMax;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_LIGHT_BACKLIGHT_H
