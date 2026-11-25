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
#ifndef MOCHA_LIGHT_PULSE_PROGRAM_H
#define MOCHA_LIGHT_PULSE_PROGRAM_H

#include <cstdint>
#include <string>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

/*
 * A pulsing light, written as a program for one channel of the lp5521.
 *
 * Each channel has an engine of its own with sixteen instructions of memory.
 * The program breathes: the channel rises evenly from dark to its brightness,
 * holds for a moment, falls back as evenly, rests for a moment and starts
 * over -- the shape of the pattern the board's firmware file used to hold,
 * but up to the brightness asked for instead of to a fixed half, and over
 * the period asked for instead of a fixed two and a half seconds. The time
 * on and the time off together make the period; a breath has no time off.
 *
 * A channel at a brightness of its own climbs at a speed of its own, and
 * rounding leaves each with a slightly different period. So the channels
 * of one colour meet at the top of every breath -- each tells the others
 * it is there and waits until all of them are -- and never drift apart.
 *
 * channel is 0 for red, 1 for green and 2 for blue; together has a bit set
 * for every channel the colour lights, this one's included. Returned as the
 * hex text the driver's engineN_load takes.
 */
class PulseProgram {
  public:
    static std::string forChannel(int channel, uint8_t together, uint8_t brightness,
                                  uint32_t periodMs);
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_LIGHT_PULSE_PROGRAM_H
