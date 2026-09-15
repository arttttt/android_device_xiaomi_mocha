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

#include <unistd.h>

#include <log/log.h>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

/* Long enough for the host to see the device go before it comes back as
 * something else. */
static constexpr useconds_t kDisconnectWaitUs = 100000;

/*
 * Take the gadget off the bus and empty its configuration.
 *
 * The monitor goes first, before anything is unlinked. Unlinking a FunctionFS
 * function unbinds it, which resets the instance, which makes the daemon write
 * its descriptors again -- and the endpoint files reappear within
 * milliseconds. A monitor still running at that moment sees exactly what it is
 * watching for and binds the controller to a configuration being taken apart.
 * The host then asks for a descriptor of a function that is no longer there,
 * and the kernel dereferences what used to be it.
 *
 * Unbinding before touching the configuration is what makes the rest legal:
 * configfs refuses to change a bound gadget.
 */
Status UsbGadget::tearDown() {
    mMonitor.stop();
    mConfig.unbind();
    mConfig.resetDeviceClass();

    return mConfig.unlinkAll() ? Status::SUCCESS : Status::ERROR;
}

Status UsbGadget::compose(uint64_t functions, const sp<IUsbGadgetCallback>& callback,
                          uint64_t timeoutMs) {
    if (!mConfig.setIdentity(functions)) return Status::ERROR;

    std::vector<std::string> endpoints = mConfig.link(functions);

    if (!mConfig.setConfigurationName(functions)) return Status::ERROR;

    /* Nothing in this configuration waits on a daemon, so the controller can
     * be written at once and the answer is already known. */
    if (endpoints.empty()) {
        if (!mConfig.bind()) return Status::ERROR;

        mApplied = true;
        if (callback) callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS);
        return Status::SUCCESS;
    }

    if (!mMonitor.start(endpoints, mConfig)) return Status::ERROR;

    if (callback) {
        /* Answered either way. A daemon that has not arrived within the time
         * the caller allowed is not a failure of the configuration -- the
         * monitor goes on waiting and binds when it does -- but the caller
         * asked for an answer by now and is owed one. */
        if (!mMonitor.waitForBind(timeoutMs))
            ALOGI("no daemon yet; the monitor goes on waiting for one");

        mApplied = mMonitor.bound();
        callback->setCurrentUsbFunctionsCb(
                functions, mApplied ? Status::SUCCESS : Status::ERROR);
    }

    return Status::SUCCESS;
}

Return<void> UsbGadget::setCurrentUsbFunctions(uint64_t functions,
                                              const sp<IUsbGadgetCallback>& callback,
                                              uint64_t timeoutMs) {
    std::lock_guard<std::mutex> lock(mLock);

    mFunctions = functions;
    mApplied = false;

    Status status = tearDown();

    if (status == Status::SUCCESS) {
        usleep(kDisconnectWaitUs);

        if (functions == static_cast<uint64_t>(GadgetFunction::NONE)) {
            if (callback) callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS);
            return Void();
        }

        status = compose(functions, callback, timeoutMs);
    }

    if (status != Status::SUCCESS) {
        ALOGE("could not set the functions");
        if (callback) callback->setCurrentUsbFunctionsCb(functions, status);
    }

    return Void();
}

Return<void> UsbGadget::getCurrentUsbFunctions(const sp<IUsbGadgetCallback>& callback) {
    if (callback)
        callback->getCurrentUsbFunctionsCb(
                mFunctions,
                mApplied ? Status::FUNCTIONS_APPLIED : Status::FUNCTIONS_NOT_APPLIED);

    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
