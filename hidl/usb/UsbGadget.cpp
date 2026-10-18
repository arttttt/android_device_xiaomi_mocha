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

#include "UsbGadget.h"

#include <log/log.h>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

namespace function = ::mocha::usb::function;
using ::mocha::usb::Result;

static_assert(static_cast<uint64_t>(GadgetFunction::NONE) == function::kNone);
static_assert(static_cast<uint64_t>(GadgetFunction::ADB) == function::kAdb);
static_assert(static_cast<uint64_t>(GadgetFunction::ACCESSORY) == function::kAccessory);
static_assert(static_cast<uint64_t>(GadgetFunction::MTP) == function::kMtp);
static_assert(static_cast<uint64_t>(GadgetFunction::MIDI) == function::kMidi);
static_assert(static_cast<uint64_t>(GadgetFunction::PTP) == function::kPtp);
static_assert(static_cast<uint64_t>(GadgetFunction::RNDIS) == function::kRndis);
static_assert(static_cast<uint64_t>(GadgetFunction::AUDIO_SOURCE) == function::kAudioSource);

Return<void> UsbGadget::setCurrentUsbFunctions(uint64_t functions,
                                              const sp<IUsbGadgetCallback>& callback,
                                              uint64_t timeoutMs) {
    /* usbd calls this at boot with no callback and no time to wait. */
    Result result = mGadget.setFunctions(functions, callback ? timeoutMs : 0);

    if (result == Result::kError) ALOGE("could not set the functions");

    if (callback)
        callback->setCurrentUsbFunctionsCb(
                functions, result == Result::kApplied ? Status::SUCCESS : Status::ERROR);

    return Void();
}

Return<void> UsbGadget::getCurrentUsbFunctions(const sp<IUsbGadgetCallback>& callback) {
    if (callback)
        callback->getCurrentUsbFunctionsCb(
                mGadget.functions(),
                mGadget.applied() ? Status::FUNCTIONS_APPLIED : Status::FUNCTIONS_NOT_APPLIED);

    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
