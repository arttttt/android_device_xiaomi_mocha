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
#ifndef MOCHA_VIBRATOR_EFFECTS_H
#define MOCHA_VIBRATOR_EFFECTS_H

#include <android/hardware/vibrator/1.2/types.h>

#include <cstdint>
#include <vector>

#include "Actuator.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_2 {
namespace implementation {

using ::android::hardware::vibrator::V1_0::EffectStrength;
using ::android::hardware::vibrator::V1_2::Effect;

/*
 * What the named effects are made of on this board.
 *
 * Separate from the motor because these are decisions, not mechanism: how
 * long each effect runs, how much silence makes two pulses read as one
 * gesture, and what LIGHT, MEDIUM and STRONG come out as. The motor would
 * play any other answer just as willingly.
 */
class Effects {
  public:
    struct Shape {
        std::vector<Step> steps;

        /* What the whole thing lasts, silence in the middle included. The
         * caller schedules against this, so an effect that gives back the
         * length of its first pulse is lying to it. */
        uint32_t lengthMs;
    };

    /* Empty steps mean this board has nothing to play for that effect. */
    static Shape of(Effect effect, EffectStrength strength);

    /*
     * How long a pulse runs at each strength.
     *
     * Three numbers per effect rather than one, because on this actuator
     * strength IS duration -- see Effects.cpp for why amplitude is not the
     * dial it is on other boards.
     */
    struct Lengths {
        uint8_t light;
        uint8_t medium;
        uint8_t strong;
    };

    static uint8_t pick(const Lengths& lengths, EffectStrength strength);

    /* What a ringtone's pulses are built from. Exposed because the rhythms
     * live in their own unit and scale their beats against this. */
    static const Lengths CLICK_MS;
};

}  // namespace implementation
}  // namespace V1_2
}  // namespace vibrator
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_VIBRATOR_EFFECTS_H
