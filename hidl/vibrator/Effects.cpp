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

#define LOG_TAG "android.hardware.vibrator@1.0-service.mocha"

#include "Effects.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

/*
 * How long each of the three strengths runs for, at full amplitude.
 *
 * Strength on this actuator is duration, not voltage, and that is a
 * measurement rather than a preference. Amplitude was tried first and does
 * not work as a dial: below about 70 of the amplifier's 127 a pulse stops
 * being felt at all, and above it nothing more arrives -- 70 and 127 at the
 * same length are told apart only by which of them is nearer the threshold
 * of noticing. Length, over the same range, rises evenly and audibly to the
 * hand.
 *
 * So all three run at the maximum and differ in how long. 27 ms is present
 * but quiet, 48 is firm; 55 was tried and was too much for a tap. Below
 * about 20 nothing useful survives, because the mass of a linear actuator
 * needs that long to reach speed and a pulse that ends first is one the
 * finger never receives.
 *
 * Found by hand on the device, which is the only instrument there is for
 * this: an accelerometer would say what the motor did, not what it felt
 * like. They are three numbers in one place, so a different hand can move
 * them.
 */
static constexpr uint8_t LIGHT_MS = 27;
static constexpr uint8_t MEDIUM_MS = 35;
static constexpr uint8_t STRONG_MS = 48;

/*
 * The silence inside a double click.
 *
 * What makes two pulses read as one gesture rather than two taps. Chosen
 * when the pulses were 20 ms long and not listened to again since they grew;
 * if a double click now sounds like a rattle, this is the number that
 * decides it.
 */
static constexpr uint8_t DOUBLE_CLICK_GAP_MS = 60;

uint8_t Effects::lengthOf(EffectStrength strength) {
    switch (strength) {
        case EffectStrength::LIGHT:  return LIGHT_MS;
        case EffectStrength::MEDIUM: return MEDIUM_MS;
        case EffectStrength::STRONG: return STRONG_MS;
    }
    return MEDIUM_MS;
}

Effects::Shape Effects::of(Effect effect, EffectStrength strength) {
    const uint8_t full = Actuator::MAX_STRENGTH;
    uint8_t length = lengthOf(strength);

    switch (effect) {
        case Effect::CLICK:
            return {{{full, length}}, length};

        case Effect::DOUBLE_CLICK:
            return {{{full, length},
                     {0, DOUBLE_CLICK_GAP_MS},
                     {full, length}},
                    static_cast<uint32_t>(length) + DOUBLE_CLICK_GAP_MS + length};
    }

    return {{}, 0};
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
