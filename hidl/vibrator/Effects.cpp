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

#include "Effects.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

/*
 * What the three strengths the framework knows come out as.
 *
 * Chosen, not measured -- measuring would want an accelerometer against the
 * back of the tablet and something to compare it to, and we have neither.
 * Even spacing of the voltage is not even spacing of the sensation, since
 * perceived intensity grows more slowly than amplitude, so these lean low
 * rather than sitting at a third and two thirds. If they feel wrong in the
 * hand, the hand is the better instrument and these are one line each.
 */
static constexpr uint8_t LIGHT = 38;
static constexpr uint8_t MEDIUM = 76;
static constexpr uint8_t STRONG = Actuator::MAX_STRENGTH;

/*
 * How long one click runs, and how long the silence inside a double one.
 *
 * Both were found by hand on the device. Below about 20 ms a click does not
 * become crisper, it becomes weaker: the mass of a linear actuator needs that
 * long to reach speed, and a pulse that ends first is one the finger barely
 * feels. 12 and 15 ms were tried and rejected for exactly that.
 *
 * The gap is what makes two clicks read as one gesture rather than two
 * events. Too short and they blur into a rattle; too long and they are
 * separate taps.
 */
static constexpr uint8_t CLICK_MS = 20;
static constexpr uint8_t DOUBLE_CLICK_GAP_MS = 60;

uint8_t Effects::strengthOf(EffectStrength strength) {
    switch (strength) {
        case EffectStrength::LIGHT:  return LIGHT;
        case EffectStrength::MEDIUM: return MEDIUM;
        case EffectStrength::STRONG: return STRONG;
    }
    return MEDIUM;
}

Effects::Shape Effects::of(Effect effect, EffectStrength strength) {
    uint8_t amplitude = strengthOf(strength);

    switch (effect) {
        case Effect::CLICK:
            return {{{amplitude, CLICK_MS}}, CLICK_MS};

        case Effect::DOUBLE_CLICK:
            return {{{amplitude, CLICK_MS},
                     {0, DOUBLE_CLICK_GAP_MS},
                     {amplitude, CLICK_MS}},
                    CLICK_MS + DOUBLE_CLICK_GAP_MS + CLICK_MS};
    }

    return {{}, 0};
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
