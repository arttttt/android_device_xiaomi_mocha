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

#include <android-base/unique_fd.h>
#include <android/hardware/usb/gadget/1.0/IUsbGadget.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

using ::android::sp;
using ::android::base::unique_fd;
using ::android::hardware::Return;
using ::android::hardware::Void;

struct UsbGadget : public IUsbGadget {
    UsbGadget();

    Return<void> setCurrentUsbFunctions(uint64_t functions,
                                        const sp<IUsbGadgetCallback>& callback,
                                        uint64_t timeoutMs) override;

    Return<void> getCurrentUsbFunctions(const sp<IUsbGadgetCallback>& callback) override;

    /*
     * The monitor thread is a free function so it can be started with the
     * object as its argument, and it reaches in here. Everything it touches
     * is public for that reason, the way the implementations this follows
     * have it.
     */
    std::mutex mLock;
    std::condition_variable mCv;

    /* What was asked for, and whether the controller has taken it. */
    uint64_t mCurrentUsbFunctions;
    bool mCurrentUsbFunctionsApplied;

    /*
     * The endpoint files a daemon creates once it has written its
     * descriptors. Their presence -- all of them -- is what says a
     * FunctionFS function is ready to be bound, and their absence is what
     * says the daemon has gone. Empty when nothing in the configuration
     * needs a daemon.
     */
    std::vector<std::string> mEndpointList;

    unique_fd mInotifyFd;
    unique_fd mEventFd;
    unique_fd mEpollFd;

    std::unique_ptr<std::thread> mMonitor;
    bool mMonitorCreated;

  private:
    /* Serialises whole calls, as distinct from mLock, which the monitor also
     * takes for short stretches. */
    std::mutex mLockSetCurrentFunction;

    Status tearDownGadget();
    Status setupFunctions(uint64_t functions, const sp<IUsbGadgetCallback>& callback,
                          uint64_t timeoutMs);
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_USB_GADGET_V1_0_USBGADGET_H
