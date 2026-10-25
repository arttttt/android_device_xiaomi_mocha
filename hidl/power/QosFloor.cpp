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

#define LOG_TAG "android.hardware.power-service.mocha"

#include "QosFloor.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <log/log.h>

namespace aidl::android::hardware::power::impl::mocha {

void QosFloor::set(uint32_t khz) {
    if (khz == mKhz) return;

    if (mFd < 0) {
        /* Nothing to withdraw from a request never made. */
        if (khz == 0) return;

        mFd.reset(open(mPath, O_WRONLY | O_CLOEXEC));
        if (mFd < 0) {
            if (!mComplained) {
                mComplained = true;
                ALOGW("cannot open %s: %s; this floor will not be held", mPath, strerror(errno));
            }
            return;
        }
        mComplained = false;
    }

    /* The node takes a binary word, as the composer writes it, or a
     * hexadecimal string; the word leaves nothing to parse. */
    const int32_t word = static_cast<int32_t>(khz);
    if (write(mFd.get(), &word, sizeof(word)) != static_cast<ssize_t>(sizeof(word))) {
        ALOGE("cannot write %s: %s", mPath, strerror(errno));
        /* Closing it withdraws whatever was held, so the state below is
         * the truth whatever the write managed. */
        mFd.reset();
        mKhz = 0;
        return;
    }
    mKhz = khz;
}

}  // namespace aidl::android::hardware::power::impl::mocha
