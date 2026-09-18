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

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "KeyDisabler.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using ::vendor::lineage::touch::V1_0::implementation::KeyDisabler;

/*
 * One interface of the four this package defines. Glove mode and stylus
 * mode want a controller that can be told to expect either; the gesture
 * interface wants a panel that wakes on shapes drawn while it sleeps, and
 * this one only knows the one shape, which the framework already asks for
 * through its own setting.
 *
 * The service registers nothing at all when the node is missing, so that
 * the framework's question -- is this supported -- is answered by the
 * absence rather than by a service that says no to everything.
 */
int main() {
    android::sp<KeyDisabler> keyDisabler = new KeyDisabler();

    if (!keyDisabler->isSupported()) {
        ALOGI("no capacitive keys to disable, nothing to serve");
        return 0;
    }

    configureRpcThreadpool(1, true /*callerWillJoin*/);

    if (keyDisabler->registerAsService() != android::OK) {
        ALOGE("cannot register the key disabler service");
        return 1;
    }

    ALOGI("touch service ready");

    joinRpcThreadpool();

    ALOGE("touch service left its thread pool");
    return 1;
}
