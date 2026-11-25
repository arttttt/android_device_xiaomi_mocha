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
#ifndef MOCHA_LIGHT_NOTIFICATION_LED_H
#define MOCHA_LIGHT_NOTIFICATION_LED_H

#include <array>
#include <cstdint>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

/*
 * The three-colour diode by the speaker, driven by an lp5521.
 *
 * A steady light is three brightnesses written straight to the channels. A
 * flashing one is left to the chip: each channel the colour uses gets a
 * program for its own engine (see PulseProgram), and they are started
 * together, so the light breathes in any colour the steady one could be,
 * as bright, and at the pace asked for.
 *
 * What is currently shown is remembered, because asking for the same thing
 * again would otherwise blink the diode off and on where nothing should have
 * changed.
 */
class NotificationLed {
  public:
    void show(uint32_t colour, uint32_t onMs, uint32_t offMs);
    void off();

  private:
    /* Red, green and blue, in the order of the chip's channels. */
    using Levels = std::array<uint8_t, 3>;

    bool alreadyShowing(uint32_t colour, uint32_t onMs, uint32_t offMs) const;
    void remember(uint32_t colour, uint32_t onMs, uint32_t offMs);

    void clear() const;
    void showSteady(const Levels& levels) const;
    void showPulsing(const Levels& levels, uint32_t periodMs) const;

    bool mSet = false;
    uint32_t mColour = 0;
    uint32_t mOnMs = 0;
    uint32_t mOffMs = 0;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_LIGHT_NOTIFICATION_LED_H
