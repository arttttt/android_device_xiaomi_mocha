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
#ifndef MOCHA_USB_GADGET_H
#define MOCHA_USB_GADGET_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include <mochausb/FfsMonitor.h>
#include <mochausb/Functions.h>
#include <mochausb/GadgetConfig.h>

namespace mocha {
namespace usb {

enum class Result {
    /* The controller has the configuration. */
    kApplied,
    /* It is linked and waits for a daemon to describe itself; the monitor
     * binds when it does. */
    kPending,
    kError,
};

/*
 * The board's USB gadget, as the gadget HALs drive it. What is here is the
 * whole of it -- the configfs composition, the wait for FunctionFS daemons, the
 * binding -- so that a service is only a translation of its interface into
 * these calls, and a new version of the interface is a new translation rather
 * than a new gadget.
 *
 * One change at a time, whole: a second call arriving between the teardown and
 * the composition would find a gadget that is neither.
 */
class Gadget {
  public:
    /* Take down what is there and compose these functions instead, waiting up
     * to timeoutMs for the controller to take them. */
    Result setFunctions(uint64_t functions, uint64_t timeoutMs);

    /* What was last asked for, and whether the controller has taken it. */
    uint64_t functions() const { return mFunctions; }
    bool applied() const { return mApplied; }

  private:
    Result tearDown();
    Result compose(uint64_t functions, uint64_t timeoutMs);

    GadgetConfig mConfig;
    FfsMonitor mMonitor;

    std::atomic<uint64_t> mFunctions{function::kNone};
    std::atomic<bool> mApplied{false};

    std::mutex mLock;
};

}  // namespace usb
}  // namespace mocha

#endif  // MOCHA_USB_GADGET_H
