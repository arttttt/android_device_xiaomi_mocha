/*
 * Copyright (C) 2018 The LineageOS Project
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

#define LOG_TAG "android.hardware.light@2.0-service.mocha"

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "Light.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using android::hardware::light::V2_0::ILight;
using android::hardware::light::V2_0::implementation::Light;

/*
 * Nothing here opens a node any more.
 *
 * It used to open all nine, in nine near-identical blocks, and pass them into
 * a constructor of seven arguments where five had the same type -- so at the
 * call site red, green and blue were told apart by their position and nothing
 * else. Three of those nine blocks checked the wrong stream afterwards: two
 * tested blueLed, one tested lcdMaxBacklight, each of them the variable from
 * the block above. Nothing ever noticed, because the checks that fired were
 * on nodes that were present anyway.
 *
 * Each lamp now opens what it needs, when it needs it. Starting the service
 * is starting the service.
 */
int main() {
    android::sp<ILight> light = new Light();

    configureRpcThreadpool(1, true /*callerWillJoin*/);

    if (light->registerAsService() != android::OK) {
        ALOGE("cannot register the lights service");
        return 1;
    }

    ALOGI("lights service ready");

    joinRpcThreadpool();

    ALOGE("lights service left its thread pool");
    return 1;
}
