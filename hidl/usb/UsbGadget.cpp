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

#define LOG_TAG "android.hardware.usb.gadget@1.2-service.mocha"

#include "UsbGadget.h"

#include <log/log.h>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_2 {
namespace implementation {

namespace function = ::mocha::usb::function;
using ::mocha::usb::Result;
using ::mocha::usb::Speed;

static_assert(static_cast<uint64_t>(GadgetFunction::NONE) == function::kNone);
static_assert(static_cast<uint64_t>(GadgetFunction::ADB) == function::kAdb);
static_assert(static_cast<uint64_t>(GadgetFunction::ACCESSORY) == function::kAccessory);
static_assert(static_cast<uint64_t>(GadgetFunction::MTP) == function::kMtp);
static_assert(static_cast<uint64_t>(GadgetFunction::MIDI) == function::kMidi);
static_assert(static_cast<uint64_t>(GadgetFunction::PTP) == function::kPtp);
static_assert(static_cast<uint64_t>(GadgetFunction::RNDIS) == function::kRndis);
static_assert(static_cast<uint64_t>(GadgetFunction::AUDIO_SOURCE) == function::kAudioSource);
static_assert(static_cast<uint64_t>(GadgetFunction::NCM) == function::kNcm);

Return<void> UsbGadget::setCurrentUsbFunctions(uint64_t functions,
                                              const sp<V1_0::IUsbGadgetCallback>& callback,
                                              uint64_t timeoutMs) {
    /* usbd calls this at boot with no callback and no time to wait. */
    Result result = mGadget.setFunctions(functions, callback ? timeoutMs : 0);
    Status status;

    switch (result) {
        case Result::kApplied:
            status = Status::SUCCESS;
            break;
        case Result::kNotSupported:
            ALOGE("no instance for functions %#llx",
                  static_cast<unsigned long long>(functions & ~function::kSupported));
            status = Status::CONFIGURATION_NOT_SUPPORTED;
            break;
        case Result::kError:
            ALOGE("could not set the functions");
            status = Status::ERROR;
            break;
        default:
            status = Status::ERROR;
            break;
    }

    if (callback) callback->setCurrentUsbFunctionsCb(functions, status);

    return Void();
}

Return<void> UsbGadget::getCurrentUsbFunctions(const sp<V1_0::IUsbGadgetCallback>& callback) {
    if (callback)
        callback->getCurrentUsbFunctionsCb(
                mGadget.functions(),
                mGadget.applied() ? Status::FUNCTIONS_APPLIED : Status::FUNCTIONS_NOT_APPLIED);

    return Void();
}

Return<Status> UsbGadget::reset() {
    return mGadget.reset() == Result::kError ? Status::ERROR : Status::SUCCESS;
}

Return<void> UsbGadget::getUsbSpeed(const sp<V1_2::IUsbGadgetCallback>& callback) {
    UsbSpeed speed;

    switch (mGadget.speed()) {
        case Speed::kLow:
            speed = UsbSpeed::LOWSPEED;
            break;
        case Speed::kFull:
            speed = UsbSpeed::FULLSPEED;
            break;
        case Speed::kHigh:
            speed = UsbSpeed::HIGHSPEED;
            break;
        case Speed::kSuper:
            speed = UsbSpeed::SUPERSPEED;
            break;
        default:
            speed = UsbSpeed::UNKNOWN;
            break;
    }

    if (callback) callback->getUsbSpeedCb(speed);

    return Void();
}

}  // namespace implementation
}  // namespace V1_2
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
