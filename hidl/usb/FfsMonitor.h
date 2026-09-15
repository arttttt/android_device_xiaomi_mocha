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
#ifndef MOCHA_USB_FFS_MONITOR_H
#define MOCHA_USB_FFS_MONITOR_H

#include <android-base/unique_fd.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "GadgetConfig.h"

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

using ::android::base::unique_fd;

/*
 * Waits for the daemons a configuration needs, and binds the controller when
 * they are ready.
 *
 * Binding is not part of answering the call that asked for the configuration.
 * Writing UDC needs the FunctionFS endpoint files to exist, and they do not
 * exist until a daemon has written its descriptors, which happens on its own
 * schedule -- more than a second, measured, when it has to notice its old
 * endpoints have stopped working first.
 *
 * So the caller links the functions and leaves this running. It also keeps
 * running afterwards, because the endpoints can go away again: a daemon that
 * closes ep0 takes the gadget down with it, since f_fs unregisters rather than
 * reset an instance a gadget still holds. Their return is a new daemon that
 * has described itself, and the controller then wants writing again. One path
 * serves the first bind and every later one, which is why a restart of adbd
 * needs no case of its own.
 */
class FfsMonitor {
  public:
    ~FfsMonitor();

    /* Watch for these endpoint files and bind through this configuration when
     * they are all there. Returns false if the watch could not be set up. */
    bool start(const std::vector<std::string>& endpoints, const GadgetConfig& config);

    /* Stop watching and join. Safe to call when nothing was started. */
    void stop();

    /* Wait for the first bind, for as long as the caller was given to answer
     * in. False means it has not happened yet -- not that it never will. */
    bool waitForBind(uint64_t timeoutMs);

    bool bound() const;

  private:
    void run(const GadgetConfig& config);
    bool endpointsPresent() const;
    void announceBound();

    std::vector<std::string> mEndpoints;

    unique_fd mInotifyFd;
    unique_fd mEventFd;
    unique_fd mEpollFd;

    std::unique_ptr<std::thread> mThread;

    mutable std::mutex mLock;
    std::condition_variable mBoundCv;
    bool mBound = false;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_USB_FFS_MONITOR_H
