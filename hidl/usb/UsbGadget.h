/*
 * Copyright (C) 2026 The LineageOS Project
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
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

#include <mutex>
#include <string>
#include <thread>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

using ::android::sp;
using ::android::hardware::hidl_string;
using ::android::hardware::Return;
using ::android::hardware::Void;

struct UsbGadget : public IUsbGadget {
    UsbGadget();

    Return<void> setCurrentUsbFunctions(uint64_t functions,
                                        const sp<IUsbGadgetCallback>& callback,
                                        uint64_t timeoutMs) override;

    Return<void> getCurrentUsbFunctions(const sp<IUsbGadgetCallback>& callback) override;

  private:
    /* Guards everything below, because the monitor thread reads and writes it
     * alongside the RPC calls. */
    std::mutex mLock;

    /* What is composed right now, and whether the controller took it. */
    uint64_t mCurrentFunctions;
    bool mCurrentApplied;

    /* Watches FunctionFS for adbd describing itself again. See monitorFfs(). */
    std::thread mMonitor;

    /* Take the gadget apart: unbind the controller and empty the
     * configuration. Safe to call when it is already apart. Call with mLock
     * held. */
    void tearDown();

    /* Build the configuration for `functions` and bind it. Returns false on
     * the first step that fails, having said which in the log. Call with
     * mLock held. */
    bool compose(uint64_t functions, uint64_t timeoutMs);

    /*
     * Rebuild what was composed, if something took it down behind our back.
     *
     * adbd dying does exactly that. FunctionFS cannot let an instance be
     * reset while a gadget still holds it -- ffs_data_clear() asserts on it --
     * so when the last descriptor of a departing daemon is released, f_fs
     * unregisters the gadget itself. Under the legacy path init noticed,
     * because init.svc.adbd is a property and its blocks rebuild the
     * configuration. Here nothing notices: the framework did not ask for a
     * change and so is not told of one, and the gadget stays down.
     *
     * So this watches the FunctionFS directory instead. The endpoint files
     * appear when adbd has written its descriptors and vanish when the
     * instance resets, which makes their arrival the signal that a daemon is
     * ready to be composed back in.
     */
    void monitorFfs();
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_USB_GADGET_V1_0_USBGADGET_H
