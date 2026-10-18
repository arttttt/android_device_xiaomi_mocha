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

#define LOG_TAG "mochausb"

#include <mochausb/Gadget.h>

#include <unistd.h>

#include <log/log.h>

namespace mocha {
namespace usb {

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
Result Gadget::tearDown() {
    mMonitor.stop();
    mConfig.unbind();
    mConfig.resetDeviceClass();

    return mConfig.unlinkAll() ? Result::kApplied : Result::kError;
}

Result Gadget::compose(uint64_t functions, uint64_t timeoutMs) {
    if (!mConfig.setIdentity(functions)) return Result::kError;

    mEndpoints = mConfig.link(functions);

    if (!mConfig.setConfigurationName(functions)) return Result::kError;

    /* Nothing in this configuration waits on a daemon, so the controller can
     * be written at once and the answer is already known. */
    if (mEndpoints.empty()) {
        if (!mConfig.bind()) return Result::kError;

        mApplied = true;
        return Result::kApplied;
    }

    if (!mMonitor.start(mEndpoints, mConfig)) return Result::kError;

    /* A daemon that has not arrived within the time the caller allowed is not
     * a failure of the configuration -- the monitor goes on waiting and binds
     * when it does -- but the caller asked for an answer by now and is owed
     * one. A caller that allowed no time at all is not waiting for one. */
    if (timeoutMs && !mMonitor.waitForBind(timeoutMs))
        ALOGI("no daemon yet; the monitor goes on waiting for one");

    mApplied = mMonitor.bound();
    return mApplied ? Result::kApplied : Result::kPending;
}

Result Gadget::setFunctions(uint64_t functions, uint64_t timeoutMs) {
    std::lock_guard<std::mutex> lock(mLock);

    if (functions & ~function::kSupported) return Result::kNotSupported;

    mFunctions = functions;
    mApplied = false;
    mEndpoints.clear();

    Result result = tearDown();
    if (result != Result::kApplied) return result;

    usleep(kDisconnectWaitUs);

    if (functions == function::kNone) return Result::kApplied;

    return compose(functions, timeoutMs);
}

/*
 * The configuration stays linked; only the controller lets go of it and takes
 * it back, which is all a host needs to see the device leave and arrive.
 *
 * Unbinding unbinds every function, and a FunctionFS daemon answers that by
 * writing its descriptors again, so with daemons in the configuration the bind
 * is the monitor's to make once they are back -- the same path that serves the
 * first one.
 */
Result Gadget::reset() {
    std::lock_guard<std::mutex> lock(mLock);

    mMonitor.stop();
    mConfig.unbind();
    mApplied = false;

    usleep(kDisconnectWaitUs);

    if (mFunctions == function::kNone) return Result::kApplied;

    if (mEndpoints.empty()) {
        if (!mConfig.bind()) return Result::kError;

        mApplied = true;
        return Result::kApplied;
    }

    return mMonitor.start(mEndpoints, mConfig) ? Result::kPending : Result::kError;
}

Speed Gadget::speed() const {
    std::string speed = mConfig.currentSpeed();

    if (speed == "low-speed") return Speed::kLow;
    if (speed == "full-speed") return Speed::kFull;
    if (speed == "high-speed") return Speed::kHigh;
    if (speed == "super-speed") return Speed::kSuper;

    return Speed::kUnknown;
}

}  // namespace usb
}  // namespace mocha
