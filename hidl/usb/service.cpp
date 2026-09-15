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

#define LOG_TAG "android.hardware.usb.gadget@1.0-service.mocha"

#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

#include "UsbGadget.h"

using ::android::OK;
using ::android::sp;
using ::android::status_t;
using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::joinRpcThreadpool;
using ::android::hardware::usb::gadget::V1_0::IUsbGadget;
using ::android::hardware::usb::gadget::V1_0::implementation::UsbGadget;

int main() {
    sp<IUsbGadget> service = new UsbGadget();

    configureRpcThreadpool(1, true /*callerWillJoin*/);

    status_t status = service->registerAsService();
    if (status != OK) {
        LOG(ERROR) << "cannot register as a service: " << status;
        return 1;
    }

    LOG(INFO) << "USB gadget HAL ready";
    joinRpcThreadpool();

    /* joinRpcThreadpool does not return. */
    return 1;
}
