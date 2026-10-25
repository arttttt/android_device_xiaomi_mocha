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

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "Power.h"

using aidl::android::hardware::power::impl::mocha::Power;

int main() {
    /* One binder thread, the one joined below. Every hint is a few writes
     * to sysfs or PM QoS, and none waits on another. */
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Power> service = ndk::SharedRefBase::make<Power>();

    const std::string instance = std::string(Power::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "cannot register as a service: " << status;
        return 1;
    }

    LOG(INFO) << "power HAL ready";
    ABinderProcess_joinThreadPool();

    /* joinThreadPool does not return. */
    return 1;
}
