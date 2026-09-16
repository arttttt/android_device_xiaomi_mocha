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

/*
 * How long a pulse may be if it is to be felt as its own event.
 *
 * A pulse needs roughly twice its own length of quiet before the next one
 * reads as separate rather than as more of the same -- the rule the rhythms
 * already use between their beats. Across separate requests the quiet is not
 * ours to lengthen, because the caller decides when to ask again; all that is
 * left is to make the pulse short enough that the quiet it needs fits the gap
 * it was given. A pulse plus its settling is three times the pulse, so a pulse
 * may take a third of the interval and no more.
 *
 * The rule replaced a table of three hand-found lengths and reproduces it:
 * the fastest interval measured is 23 ms, a third of which is 7.7 against the
 * 8 the hand chose, and 12 was tried and found to sit on the edge of running
 * together, which a third of 23 says it must.
 *
 * What makes it worth having in one place is that it does not care which
 * effect is asking. Two beats of a rhythm ran together when a long one was
 * followed by a short one, and they were fixed by looking at lengths rather
 * than at meanings; the same holds between requests, where a click arriving
 * thirty milliseconds after a thud has exactly the problem a texture has.
 *
 * Nothing else is in a position to do it. The framework sends each request
 * without saying when the next will come or how recently the last one did,
 * and the driver sees finished patterns rather than the stream they belong
 * to. The succession is visible only where the requests arrive.
 */
class Cadence {
  public:
    /*
     * Records this arrival and answers how long its pulse may be.
     *
     * NO_LIMIT when nothing came recently enough to matter, which is the
     * ordinary case: a gesture on its own is limited by nothing.
     */
    uint8_t mark();

    static constexpr uint8_t NO_LIMIT = 255;

  private:
    /* A pulse, and twice its length again for the mass to settle. */
    static constexpr int64_t PERIOD_PER_PULSE = 3;

    /*
     * Never shorten below this.
     *
     * Requests can arrive faster than anything can be felt -- an application
     * may ask ten times in ten milliseconds -- and the answer to that is not a
     * pulse of nothing. Below eight the mass barely leaves rest, so shortening
     * stops there and such pulses are simply allowed to run together, which is
     * the honest rendering of being asked for the impossible.
     */
    static constexpr uint8_t SHORTEST_MS = 8;

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
