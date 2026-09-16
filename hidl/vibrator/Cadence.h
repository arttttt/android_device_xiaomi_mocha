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

/* How closely an effect follows the one before it. */
enum class Pace {
    RAPID,   /* one of a stream, arriving faster than a pulse can settle */
    STEADY,  /* following something, but with room between them */
    SINGLE,  /* on its own */
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
     * Where one band ends and the next begins.
     *
     * Two bands were tried first and are too blunt for a finger, which does
     * not move at two speeds. Under fifty milliseconds is a hurry and the
     * pulses have to be small enough to leave gaps; over a hundred the
     * requests are far enough apart to be events in their own right; between
     * them is a deliberate drag, which wants something of both.
     *
     * The outer two were found by hand on the device. The middle is where a
     * band had to begin rather than a measurement of its own.
     */
    static constexpr int64_t RAPID_WITHIN_NS = 50 * 1000 * 1000;
    static constexpr int64_t STEADY_WITHIN_NS = 100 * 1000 * 1000;

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
