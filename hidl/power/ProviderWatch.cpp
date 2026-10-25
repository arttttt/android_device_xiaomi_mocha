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

#include "ProviderWatch.h"

#include <chrono>
#include <string>

#include <android-base/properties.h>
#include <log/log.h>
#include <sys/system_properties.h>

namespace aidl::android::hardware::power::impl::mocha {

namespace {

constexpr char STATE[] = "init.svc.vendor.camera-provider-2-4";

std::string read(const prop_info* pi) {
    std::string value;
    __system_property_read_callback(
            pi,
            [](void* cookie, const char*, const char* v, uint32_t) {
                *static_cast<std::string*>(cookie) = v;
            },
            &value);
    return value;
}

}  // namespace

ProviderWatch::ProviderWatch(std::function<void()> gone) : mGone(std::move(gone)) {
    std::thread(&ProviderWatch::loop, this).detach();
}

void ProviderWatch::loop() {
    /* The property exists once init has started the provider, which can
     * be after this service is up. */
    const prop_info* pi = nullptr;
    while ((pi = __system_property_find(STATE)) == nullptr) {
        ::android::base::WaitForPropertyCreation(STATE, std::chrono::hours(1));
    }

    uint32_t serial = 0;
    bool running = false;
    for (;;) {
        if (!__system_property_wait(pi, serial, &serial, nullptr)) continue;

        const bool now = read(pi) == "running";
        if (running && !now) mGone();
        running = now;
    }
}

}  // namespace aidl::android::hardware::power::impl::mocha
