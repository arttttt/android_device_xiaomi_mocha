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

#define LOG_TAG "android.hardware.power@1.3-service.mocha"

#include "BoostPulse.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <log/log.h>

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

static const char BOOSTPULSE_NODE[] =
        "/sys/devices/system/cpu/cpufreq/interactive/boostpulse";

void BoostPulse::pulse() {
    std::lock_guard<std::mutex> lock(mLock);

    if (mFd < 0) {
        mFd.reset(open(BOOSTPULSE_NODE, O_WRONLY | O_CLOEXEC));

        if (mFd < 0) {
            if (!mComplained) {
                mComplained = true;
                ALOGW("no %s; touch and launch hints will not raise the clock",
                      BOOSTPULSE_NODE);
            }
            return;
        }

        if (mComplained) {
            mComplained = false;
            ALOGI("%s opened; touch and launch hints raise the clock", BOOSTPULSE_NODE);
        }
    }

    if (write(mFd.get(), "1", 1) < 0) {
        ALOGE("cannot pulse the boost: %s", strerror(errno));

        /* Let the next pulse open it again. A governor change takes the node
         * away and brings a new one back, and a descriptor onto the old one
         * fails for ever. */
        mFd.reset();
    }
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android
