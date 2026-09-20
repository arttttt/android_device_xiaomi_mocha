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

#include "FfsMonitor.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <chrono>

#include <log/log.h>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

static constexpr char kFfsAdbDir[] = "/dev/usb-ffs/adb/";

static constexpr int kEpollEvents = 10;
static constexpr int kBufferSize = 512;

/* Written to the event descriptor to bring the thread home. Any value would
 * do; this one is recognisable in a trace. */
static constexpr uint64_t kStop = 100;

/* How long to wait before offering the controller a configuration it has
 * already refused. A controller that is not registered yet answers ENODEV,
 * which is the ordinary answer at boot: this service is up around twenty
 * seconds in, well before the UDC. Nothing knocks a second time -- the daemon
 * has its endpoints open by then, and the directory it lives in stays still --
 * so an owed bind left to the watch alone waits on an event that never comes,
 * and the gadget is never put on the bus at all. The wait backs off, so a
 * controller that truly never arrives costs almost nothing to keep waiting
 * for. */
static constexpr int kBindRetryMinMs = 100;
static constexpr int kBindRetryMaxMs = 2000;

static bool addToEpoll(const unique_fd& epollFd, const unique_fd& fd) {
    struct epoll_event event = {};

    event.data.fd = fd.get();
    event.events = EPOLLIN;

    if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, fd.get(), &event)) {
        ALOGE("cannot watch descriptor %d: %s", fd.get(), strerror(errno));
        return false;
    }
    return true;
}

FfsMonitor::~FfsMonitor() {
    stop();
}

bool FfsMonitor::start(const std::vector<std::string>& endpoints, const GadgetConfig& config) {
    unique_fd inotifyFd(inotify_init());
    unique_fd eventFd(eventfd(0, 0));
    unique_fd epollFd(epoll_create(2));

    if (inotifyFd < 0 || eventFd < 0 || epollFd < 0) {
        ALOGE("cannot create the monitor's descriptors: %s", strerror(errno));
        return false;
    }

    /* The directory rather than the files: they are created and destroyed as
     * daemons come and go, and a watch on a file that is about to be deleted
     * would go with it. */
    if (inotify_add_watch(inotifyFd.get(), kFfsAdbDir, IN_ALL_EVENTS) == -1) {
        ALOGE("cannot watch %s: %s", kFfsAdbDir, strerror(errno));
        return false;
    }

    if (!addToEpoll(epollFd, inotifyFd) || !addToEpoll(epollFd, eventFd)) return false;

    {
        std::lock_guard<std::mutex> lock(mLock);
        mEndpoints = endpoints;
        mBound = false;
    }

    mInotifyFd = std::move(inotifyFd);
    mEventFd = std::move(eventFd);
    mEpollFd = std::move(epollFd);

    mThread = std::make_unique<std::thread>(&FfsMonitor::run, this, std::cref(config));
    return true;
}

void FfsMonitor::stop() {
    if (!mThread) return;

    uint64_t stop = kStop;
    write(mEventFd.get(), &stop, sizeof(stop));

    mThread->join();
    mThread.reset();

    mInotifyFd.reset();
    mEventFd.reset();
    mEpollFd.reset();
}

bool FfsMonitor::waitForBind(uint64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mLock);

    return mBoundCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this] { return mBound; });
}

bool FfsMonitor::bound() const {
    std::lock_guard<std::mutex> lock(mLock);
    return mBound;
}

bool FfsMonitor::endpointsPresent() const {
    std::vector<std::string> endpoints;
    {
        std::lock_guard<std::mutex> lock(mLock);
        endpoints = mEndpoints;
    }

    for (const auto& endpoint : endpoints)
        if (access(endpoint.c_str(), R_OK)) return false;

    return true;
}

void FfsMonitor::announceBound() {
    std::lock_guard<std::mutex> lock(mLock);

    mBound = true;
    mBoundCv.notify_all();
}

void FfsMonitor::run(const GadgetConfig& config) {
    char buf[kBufferSize];
    struct epoll_event events[kEpollEvents];
    bool wantBind = true;
    bool stopping = false;
    int retryMs = kBindRetryMinMs;

    /* The descriptors may already be written by the time this starts. Said out
     * loud like every other bind, so that a gadget on the bus can always be
     * traced to the attempt that put it there. */
    if (endpointsPresent() && config.bind()) {
        wantBind = false;
        announceBound();
        ALOGI("gadget bound");
    }

    while (!stopping) {
        /* Waiting for the watch alone is right only while there is nothing
         * owed. With a bind outstanding on endpoints that are already there,
         * the wait is on the controller instead, and that is a matter of time
         * rather than of anything happening in the directory. */
        int timeout = wantBind && endpointsPresent() ? retryMs : -1;

        int n = epoll_wait(mEpollFd.get(), events, kEpollEvents, timeout);
        if (n < 0) continue;

        if (n == 0) {
            if (config.bind()) {
                wantBind = false;
                retryMs = kBindRetryMinMs;
                announceBound();
                ALOGI("gadget bound");
            } else if (retryMs < kBindRetryMaxMs) {
                retryMs *= 2;
                if (retryMs > kBindRetryMaxMs) retryMs = kBindRetryMaxMs;
            }
            continue;
        }

        for (int i = 0; i < n && !stopping; i++) {
            if (events[i].data.fd != mInotifyFd.get()) {
                uint64_t flag;
                if (read(mEventFd.get(), &flag, sizeof(flag)) == sizeof(flag) && flag == kStop)
                    stopping = true;
                break;
            }

            int length = read(mInotifyFd.get(), buf, sizeof(buf));
            if (length <= 0) continue;

            for (char* p = buf; p < buf + length;) {
                auto* event = reinterpret_cast<struct inotify_event*>(p);
                p += sizeof(struct inotify_event) + event->len;

                bool present = endpointsPresent();

                if (!present && !wantBind) {
                    /* The daemon has gone, and taken the binding with it. */
                    wantBind = true;
                } else if (present && wantBind && config.bind()) {
                    wantBind = false;
                    retryMs = kBindRetryMinMs;
                    announceBound();
                    ALOGI("gadget bound");
                }
            }
        }
    }
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
