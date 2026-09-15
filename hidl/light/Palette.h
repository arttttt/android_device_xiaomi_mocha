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
#ifndef MOCHA_LIGHT_PALETTE_H
#define MOCHA_LIGHT_PALETTE_H

#include <cstdint>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

struct Rgb {
    uint8_t r, g, b;
};

/*
 * The colours the notification diode can actually show while blinking, and
 * which of them is closest to one it cannot.
 *
 * The blinking is run by an engine inside the lp5521, and the engine has one
 * duty cycle for all three channels rather than one each -- so a blinking
 * light is a primary at full brightness and nothing in between. Asked for a
 * colour off that list, the nearest one has to be picked, and "nearest" is
 * not a question about numbers: 40 units of red and 40 units of blue are not
 * the same distance to the eye. So the comparison is made in L*a*b*, where
 * distance is meant to correspond to how different two colours look.
 *
 * Steady light is not restricted this way and does not come through here.
 */
class Palette {
  public:
    static Rgb nearest(Rgb colour);
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_LIGHT_PALETTE_H
