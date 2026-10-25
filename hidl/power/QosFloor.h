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

#ifndef MOCHA_POWER_QOS_FLOOR_H
#define MOCHA_POWER_QOS_FLOOR_H

#include <android-base/unique_fd.h>

#include <cstdint>

namespace aidl::android::hardware::power::impl::mocha {

/*
 * One request for a lowest clock, held through a PM QoS node: /dev/cpu_freq_min,
 * /dev/gpu_freq_min or /dev/emc_freq_min, in kilohertz.
 *
 * A request is the descriptor itself. The kernel keeps one per open file and
 * honours the highest of them, so this one stands beside the composer's own
 * on the same nodes without either overwriting the other: each only ever
 * asks for at least so much, and closing a descriptor withdraws exactly its
 * own request. That is also why a crash of this service cannot leave a floor
 * behind -- the descriptors go with the process.
 *
 * Opened at the first request rather than at construction, so a node that is
 * missing costs one warning when it is first wanted, not one at every start.
 */
class QosFloor {
  public:
    explicit QosFloor(const char* path) : mPath(path) {}

    QosFloor(const QosFloor&) = delete;
    QosFloor& operator=(const QosFloor&) = delete;

    /* Ask for at least khz; nought withdraws the request. */
    void set(uint32_t khz);

    uint32_t value() const { return mKhz; }

  private:
    const char* mPath;
    ::android::base::unique_fd mFd;
    uint32_t mKhz = 0;
    bool mComplained = false;
};

}  // namespace aidl::android::hardware::power::impl::mocha

#endif  // MOCHA_POWER_QOS_FLOOR_H
