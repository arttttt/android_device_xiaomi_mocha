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
#ifndef ANDROID_HARDWARE_USB_GADGET_V1_2_USBGADGET_H
#define ANDROID_HARDWARE_USB_GADGET_V1_2_USBGADGET_H

#include <android/hardware/usb/gadget/1.2/IUsbGadget.h>
#include <android/hardware/usb/gadget/1.2/IUsbGadgetCallback.h>
#include <hidl/Status.h>

#include <mochausb/Gadget.h>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_2 {
namespace implementation {

using ::android::sp;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::usb::gadget::V1_0::Status;

/*
 * What UsbDeviceManager calls when the set of USB functions changes.
 *
 * It is here rather than left to init because the legacy path routes every
 * change through the "none" configuration, whose block in
 * init.usb.configfs.rc stops adbd unconditionally -- so adb is torn down and
 * rebuilt even when it is in both the old set and the new one. UsbHandlerHal
 * stops adbd only when the new configuration has no ADB in it.
 *
 * 1.2 rather than 1.0 because S calls the newer methods whenever the service
 * can be cast to them: reset() through 1.1 and getUsbSpeed() through 1.2. The
 * reset is not even guarded -- it casts and calls, and against 1.0 the cast
 * gives null.
 *
 * The gadget itself is libmochausb's. This only answers in HIDL.
 */
struct UsbGadget : public IUsbGadget {
    Return<void> setCurrentUsbFunctions(uint64_t functions,
                                        const sp<V1_0::IUsbGadgetCallback>& callback,
                                        uint64_t timeoutMs) override;

    Return<void> getCurrentUsbFunctions(const sp<V1_0::IUsbGadgetCallback>& callback) override;

    Return<Status> reset() override;

    Return<void> getUsbSpeed(const sp<V1_2::IUsbGadgetCallback>& callback) override;

  private:
    ::mocha::usb::Gadget mGadget;
};

}  // namespace implementation
}  // namespace V1_2
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_USB_GADGET_V1_2_USBGADGET_H
