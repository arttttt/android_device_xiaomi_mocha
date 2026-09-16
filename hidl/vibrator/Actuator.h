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
#ifndef MOCHA_VIBRATOR_ACTUATOR_H
#define MOCHA_VIBRATOR_ACTUATOR_H

#include <cstdint>
#include <utility>
#include <vector>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_3 {
namespace implementation {

/* One step of a pattern: how hard, and for how long. A strength of zero is a
 * pause. */
using Step = std::pair<uint8_t, uint8_t>;

/*
 * The motor, and the three ways this board can drive it.
 *
 * Running for a while is one thing and playing a shape is another, and they
 * do not share a path: a run takes its strength from whatever was last set,
 * while a pattern carries its own and is timed by the driver rather than by
 * this process. Keeping them apart is what lets an effect play without
 * disturbing the strength the framework chose for ordinary vibration.
 */
class Actuator {
  public:
    Actuator();

    /* Run at the strength last set, for this long. */
    bool run(uint32_t milliseconds) const;
    bool stop() const;

    /* The strength ordinary running uses, on the interface's scale of 1..255. */
    bool setStrength(uint8_t amplitude) const;

    /* Whether the strength can be set at all, asked of the node rather than
     * assumed -- the framework changes what it does with the answer. */
    bool strengthAdjustable() const { return mStrengthAdjustable; }

    /* Play a shape, timed by the driver. */
    bool play(const std::vector<Step>& steps) const;

    /* Where strength ends. Not a choice: the amplifier takes a signed
     * real-time playback value and its input is configured bidirectional, so
     * this is 100% of rated voltage and there is nothing above it to ask
     * for. The driver clamps to the same number. */
    static constexpr uint8_t MAX_STRENGTH = 127;

  private:
    const bool mStrengthAdjustable;
};

}  // namespace implementation
}  // namespace V1_3
}  // namespace vibrator
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_VIBRATOR_ACTUATOR_H
