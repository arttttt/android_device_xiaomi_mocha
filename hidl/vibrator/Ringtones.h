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
#ifndef MOCHA_VIBRATOR_RINGTONES_H
#define MOCHA_VIBRATOR_RINGTONES_H

#include <cstdint>

#include "Effects.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_3 {
namespace implementation {

/*
 * The fifteen ringtone slots.
 *
 * Their own unit because they are a different kind of thing from the named
 * effects: a click is one pulse and a decision about its length, a ringtone
 * is a rhythm. They change for different reasons and at different times.
 */
class Ringtones {
  public:
    /*
     * The rhythm for a ringtone effect, with its pulses measured against
     * pulseMs. Anything that is not one of the fifteen comes back empty.
     */
    static Effects::Shape of(Effect effect, uint8_t pulseMs);
};

}  // namespace implementation
}  // namespace V1_3
}  // namespace vibrator
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_VIBRATOR_RINGTONES_H
