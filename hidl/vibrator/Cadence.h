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
#ifndef MOCHA_VIBRATOR_CADENCE_H
#define MOCHA_VIBRATOR_CADENCE_H

#include <atomic>
#include <cstdint>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_3 {
namespace implementation {

/* Whether an effect arrived on its own or in the middle of a stream. */
enum class Pace {
    SINGLE,
    REPEATED,
};

/*
 * How quickly the same effect is being asked for.
 *
 * Some effects are defined in terms of repetition -- a texture is a texture
 * because it arrives while the finger moves -- and they want a different shape
 * depending on whether they got it. Nothing outside this process can answer
 * that: the framework sends identical requests and says nothing about pace,
 * so the only place the succession is visible is where the requests land.
 *
 * Kept apart from the table of effect shapes because it is a different kind of
 * knowledge. The shapes are decisions about this motor and hold whenever they
 * are read; this is an observation about what the caller is doing right now.
 */
class Cadence {
  public:
    /* Records this arrival and reports how it stands to the one before it. */
    Pace mark();

  private:
    /*
     * Below this, treat the arrival as part of a stream.
     *
     * Arithmetic put it at fifty: a pulse that has to read as its own event
     * needs roughly its own length again before the mass has settled, which is
     * the rule the rhythms use, and the longest thing that answers to a pace
     * here is twenty. The hand disagreed -- at fifty the change came only when
     * the finger was already moving fast, and the firmer pulse had been
     * overstaying well before that. Ten arrivals a second is plainly a finger
     * in motion, so that is where it switches now.
     *
     * The arithmetic was not wrong about when two pulses merge. It was
     * answering a different question from the one that matters here, which is
     * when a stream starts feeling like one.
     */
    static constexpr int64_t REPEATED_WITHIN_NS = 100 * 1000 * 1000;

    /* Atomic rather than guarded, because the answer has to be right whatever
     * the size of the thread pool, and a pool of one is a fact about today. */
    std::atomic<int64_t> mLastNs{0};
};

}  // namespace implementation
}  // namespace V1_3
}  // namespace vibrator
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_VIBRATOR_CADENCE_H
