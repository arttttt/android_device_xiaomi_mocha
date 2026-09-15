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
#ifndef MOCHA_POWER_BOOST_PULSE_H
#define MOCHA_POWER_BOOST_PULSE_H

#include <android-base/unique_fd.h>

#include <mutex>

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

/*
 * The interactive governor's boostpulse: one write says "someone is waiting",
 * and the governor raises the clock and drops it again after a duration it was
 * configured with. How long a touch is worth is therefore the performance
 * profile's to decide, not this service's.
 *
 * The descriptor is kept because a pulse arrives on every touch and reopening
 * a file that often is work for nothing. It is opened late rather than at
 * construction: the node exists only while the interactive governor is in
 * charge, and init hands it over after choosing one, so the first hints of a
 * boot can legitimately arrive before there is anything to open.
 */
class BoostPulse {
  public:
    void pulse();

  private:
    std::mutex mLock;
    base::unique_fd mFd;

    /* Whether the absence of the node has already been reported. Without
     * this it would be reported on every touch, which costs more than what
     * it tells; without reporting the return, silence would mean both
     * "working" and "never worked", which is how this node stayed unwritable
     * for months. */
    bool mComplained = false;
};

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_POWER_BOOST_PULSE_H
