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
#ifndef ANDROID_HARDWARE_USB_GADGET_V1_0_USBGADGET_H
#define ANDROID_HARDWARE_USB_GADGET_V1_0_USBGADGET_H

#include <android/hardware/usb/gadget/1.0/IUsbGadget.h>
#include <hidl/Status.h>

#include <mutex>

#include "FfsMonitor.h"
#include "GadgetConfig.h"

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

using ::android::sp;
using ::android::hardware::Return;
using ::android::hardware::Void;

/*
 * What UsbDeviceManager calls when the set of USB functions changes.
 *
 * It is here rather than left to init because the legacy path routes every
 * change through the "none" configuration, whose block in
 * init.usb.configfs.rc stops adbd unconditionally -- so adb is torn down and
 * rebuilt even when it is in both the old set and the new one. UsbHandlerHal
 * stops adbd only when the new configuration has no ADB in it.
 *
 * This decides what should be composed and in what order. GadgetConfig does
 * the composing, FfsMonitor waits for the daemons and binds.
 */
struct UsbGadget : public IUsbGadget {
    Return<void> setCurrentUsbFunctions(uint64_t functions,
                                        const sp<IUsbGadgetCallback>& callback,
                                        uint64_t timeoutMs) override;

    Return<void> getCurrentUsbFunctions(const sp<IUsbGadgetCallback>& callback) override;

  private:
    Status tearDown();
    Status compose(uint64_t functions, const sp<IUsbGadgetCallback>& callback,
                   uint64_t timeoutMs);

    GadgetConfig mConfig;
    FfsMonitor mMonitor;

    /* What was last asked for, and whether the controller has taken it. */
    uint64_t mFunctions = 0;
    bool mApplied = false;

    /* One change at a time, whole: a second call arriving between the
     * teardown and the composition would find a gadget that is neither. */
    std::mutex mLock;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_USB_GADGET_V1_0_USBGADGET_H
