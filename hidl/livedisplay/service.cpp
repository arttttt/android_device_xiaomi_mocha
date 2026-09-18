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

#define LOG_TAG "vendor.lineage.livedisplay@2.0-service.mocha"

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "DisplayModes.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using ::vendor::lineage::livedisplay::V2_0::IDisplayModes;
using ::vendor::lineage::livedisplay::V2_0::implementation::DisplayModes;

/*
 * One interface, and the only one of LiveDisplay's nine this board can
 * answer for itself. The rest -- adaptive backlight, sunlight enhancement,
 * picture adjustment and the others -- want blocks in a panel this one does
 * not have; colour calibration and reading mode the framework already does
 * on its own, through the same colour matrix this profile rides on.
 */
int main() {
    android::sp<DisplayModes> modes = new DisplayModes();

    configureRpcThreadpool(1, true /*callerWillJoin*/);

    if (modes->registerAsService() != android::OK) {
        ALOGE("cannot register the display modes service");
        return 1;
    }

    ALOGI("display modes service ready");

    joinRpcThreadpool();

    ALOGE("display modes service left its thread pool");
    return 1;
}
