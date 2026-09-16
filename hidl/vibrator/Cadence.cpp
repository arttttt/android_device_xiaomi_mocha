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

#define LOG_TAG "android.hardware.vibrator@1.3-service.mocha"

#include "Cadence.h"

#include <chrono>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_3 {
namespace implementation {

Pace Cadence::mark() {
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();

    /* Exchange rather than read-then-write: two arrivals must not both be
     * measured against the same predecessor. */
    const int64_t previous = mLastNs.exchange(now);

    /* The first arrival of all has nothing behind it, and a gesture that
     * begins is a single event however it continues. */
    if (previous == 0) return Pace::SINGLE;

    return (now - previous) < REPEATED_WITHIN_NS ? Pace::REPEATED : Pace::SINGLE;
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
