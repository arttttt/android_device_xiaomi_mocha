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

#define LOG_TAG "vendor.lineage.touch@1.0-service.mocha"

#include "KeyDisabler.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <log/log.h>
#include <unistd.h>

#include <string>

namespace vendor {
namespace lineage {
namespace touch {
namespace V1_0 {
namespace implementation {

namespace {

/* A symlink the touch driver puts up, pointing at enable_keys on the
 * controller's own i2c node. It counts the keys as on, not the disabler:
 * one means they work. */
constexpr const char kControlPath[] = "/proc/touchpanel/capacitive_keys_enable";

constexpr const char kKeysOn[] = "1";
constexpr const char kKeysOff[] = "0";

}  // anonymous namespace

KeyDisabler::KeyDisabler() : mSupported(access(kControlPath, F_OK) == 0) {
}

bool KeyDisabler::isSupported() const {
    return mSupported;
}

Return<bool> KeyDisabler::isEnabled() {
    if (!mSupported) {
        return false;
    }

    std::string buf;
    if (!::android::base::ReadFileToString(kControlPath, &buf, true)) {
        ALOGE("could not read %s", kControlPath);
        return false;
    }

    /* Inverted on purpose: the node says the keys are on, the caller asks
     * whether the disabler is. */
    return ::android::base::Trim(buf) == kKeysOff;
}

Return<bool> KeyDisabler::setEnabled(bool enabled) {
    if (!mSupported) {
        return false;
    }

    const char* wanted = enabled ? kKeysOff : kKeysOn;
    if (!::android::base::WriteStringToFile(wanted, kControlPath, true)) {
        ALOGE("could not write %s to %s", wanted, kControlPath);
        return false;
    }

    return true;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace touch
}  // namespace lineage
}  // namespace vendor
