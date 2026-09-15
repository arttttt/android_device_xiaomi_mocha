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

#include <android/hardware/vibrator/1.0/types.h>

#include <cstdint>
#include <vector>

#include "Actuator.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

/*
 * What the named effects are made of on this board.
 *
 * Separate from the motor because these are decisions, not mechanism: how
 * long a click runs, how much silence makes two of them read as one gesture,
 * and what LIGHT, MEDIUM and STRONG come out as. The motor would play any
 * other answer just as willingly.
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

  private:
    /* How long the pulse runs for a given strength. On this actuator that
     * is what strength means -- see Effects.cpp. */
    static uint8_t lengthOf(EffectStrength strength);
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_VIBRATOR_EFFECTS_H
