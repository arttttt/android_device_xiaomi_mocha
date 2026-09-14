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

/*
 * What this service is for.
 *
 * UsbDeviceManager composes a gadget in one of two ways and picks between them
 * by asking hwservicemanager for IUsbGadget. Without one it uses
 * UsbHandlerLegacy, writes sys.usb.config and leaves the work to init. With one
 * it uses UsbHandlerHal and calls here.
 *
 * The reason to be here is that the legacy path routes every change through the
 * "none" configuration, whose block in init.usb.configfs.rc stops adbd
 * unconditionally, so adb is torn down and rebuilt even when it is in both the
 * old set of functions and the new one. UsbHandlerHal stops adbd only when the
 * new configuration has no ADB in it, so switching between two that both carry
 * adb leaves the daemon alone.
 *
 * The shape of this file follows the implementations that ship on devices which
 * have had this HAL since it existed -- wahoo, coral, barbet. What that shape
 * gets right, and what the interface does not suggest, is that binding the
 * controller is not part of answering the call. Writing UDC needs the
 * FunctionFS endpoints to exist, and they do not exist until a daemon has
 * written its descriptors, which happens on its own schedule. So the call links
 * the functions and hands the rest to a monitor thread, and the monitor is what
 * writes UDC -- once at first, and again whenever a daemon goes away and comes
 * back. One path serves both, which is why a restart of adbd needs no special
 * case of its own.
 */

#define LOG_TAG "android.hardware.usb.gadget@1.0-service.mocha"

#include "UsbGadget.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>

#include <ctype.h>
#include <dirent.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

using ::android::base::GetProperty;
using ::android::base::StringPrintf;
using ::android::base::WriteStringToFile;

static constexpr char kGadget[] = "/config/usb_gadget/g1";
static constexpr char kConfig[] = "/config/usb_gadget/g1/configs/b.1";
static constexpr char kUdc[] = "/config/usb_gadget/g1/UDC";
static constexpr char kFfsAdbDir[] = "/dev/usb-ffs/adb/";

/*
 * The instance names are not ours to choose. init.tn8.usb.rc created them, and
 * the platform's init.usb.configfs.rc symlinks exactly these, so keeping them
 * means anything that falls back to that file still finds what it expects. ptp
 * is gs1 rather than gs0 because mtp and ptp are two instances of one driver,
 * numbered in turn.
 */
static constexpr char kFfsAdb[] = "ffs.adb";
static constexpr char kMtp[] = "mtp.gs0";
static constexpr char kPtp[] = "ptp.gs1";
static constexpr char kAccessory[] = "accessory.gs2";
static constexpr char kAudioSource[] = "audio_source.gs3";
static constexpr char kRndis[] = "rndis.gs4";

/* Long enough for the host to see the device go before it comes back as
 * something else. The implementations this follows all wait here. */
static constexpr useconds_t kDisconnectWaitUs = 100000;

static constexpr int kEpollEvents = 10;
static constexpr int kBufferSize = 512;
static constexpr uint64_t kStopMonitor = 100;

/* Set once the controller has been written, read by the call waiting for it.
 * Guarded by UsbGadget::mLock. */
static volatile bool gGadgetPullup;

static std::string controllerName() {
    /*
     * udc-core names the entry in /sys/class/udc after the parent device, and
     * board-ardbeg.c gives the device probed from udc@7d000000 the legacy name
     * tegra-udc.0 through OF_DEV_AUXDATA. init.tn8.usb.rc puts that name in the
     * property; /sys/class/udc is the final word on it.
     */
    return GetProperty("sys.usb.controller", "");
}

static bool endpointsPresent(const std::vector<std::string>& endpoints) {
    for (const auto& endpoint : endpoints)
        if (access(endpoint.c_str(), R_OK)) return false;
    return true;
}

static bool pullUp() {
    std::string controller = controllerName();

    if (controller.empty()) {
        LOG(ERROR) << "sys.usb.controller is not set";
        return false;
    }
    if (!WriteStringToFile(controller, kUdc)) {
        PLOG(ERROR) << "cannot bind " << controller;
        return false;
    }
    return true;
}

/*
 * Waits for the endpoints of every FunctionFS function in the configuration to
 * appear, and binds the controller when they have.
 *
 * It keeps running afterwards, because they can go away again. When a daemon
 * dies, f_fs unregisters the gadget rather than reset an instance a gadget
 * still holds -- ffs_data_clear() asserts on exactly that -- and the endpoint
 * files go with it. Their return is a new daemon that has described itself, and
 * the controller then wants writing again. writeUdc is what makes that
 * edge-triggered rather than repeated on every event.
 */
static void monitorFfs(UsbGadget* gadget) {
    char buf[kBufferSize];
    bool writeUdc = true, stopMonitor = false;
    struct epoll_event events[kEpollEvents];

    /* The descriptors may already be written by the time we get here. */
    if (endpointsPresent(gadget->mEndpointList) && pullUp()) {
        std::lock_guard<std::mutex> lock(gadget->mLock);
        gadget->mCurrentUsbFunctionsApplied = true;
        writeUdc = false;
        gGadgetPullup = true;
        gadget->mCv.notify_all();
    }

    while (!stopMonitor) {
        int n = epoll_wait(gadget->mEpollFd, events, kEpollEvents, -1);
        if (n <= 0) continue;

        for (int i = 0; i < n && !stopMonitor; i++) {
            if (events[i].data.fd != gadget->mInotifyFd) {
                uint64_t flag;
                if (read(gadget->mEventFd, &flag, sizeof(flag)) == sizeof(flag) &&
                    flag == kStopMonitor)
                    stopMonitor = true;
                break;
            }

            int len = read(gadget->mInotifyFd, buf, sizeof(buf));
            if (len <= 0) continue;

            for (char* p = buf; p < buf + len;) {
                auto* event = reinterpret_cast<struct inotify_event*>(p);
                p += sizeof(struct inotify_event) + event->len;

                bool present = endpointsPresent(gadget->mEndpointList);

                if (!present && !writeUdc) {
                    /* The daemon has gone, and taken the binding with it. */
                    writeUdc = true;
                } else if (present && writeUdc && pullUp()) {
                    std::lock_guard<std::mutex> lock(gadget->mLock);
                    gadget->mCurrentUsbFunctionsApplied = true;
                    writeUdc = false;
                    gGadgetPullup = true;
                    gadget->mCv.notify_all();
                    LOG(INFO) << "gadget bound";
                }
            }
        }
    }
}

UsbGadget::UsbGadget()
    : mCurrentUsbFunctions(static_cast<uint64_t>(GadgetFunction::NONE)),
      mCurrentUsbFunctionsApplied(false),
      mMonitorCreated(false) {}

static bool linkFunction(const char* function, int index) {
    std::string from = StringPrintf("%s/functions/%s", kGadget, function);
    std::string to = StringPrintf("%s/f%d", kConfig, index);

    if (symlink(from.c_str(), to.c_str())) {
        PLOG(ERROR) << "cannot link " << function << " as f" << index;
        return false;
    }
    return true;
}

static bool unlinkFunctions() {
    DIR* dir = opendir(kConfig);
    struct dirent* entry;
    bool ok = true;

    if (!dir) {
        PLOG(ERROR) << "cannot read " << kConfig;
        return false;
    }

    /* d_type is not reported in configfs, so the name is what there is to go
     * on -- and every link this service makes is called fN. */
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != 'f' || !isdigit(entry->d_name[1])) continue;

        std::string path = StringPrintf("%s/%s", kConfig, entry->d_name);
        if (unlink(path.c_str())) {
            PLOG(ERROR) << "cannot unlink " << path;
            ok = false;
        }
    }
    closedir(dir);
    return ok;
}

/*
 * The product id this board has always reported for each combination. The
 * platform never writes one, so it is ours to keep, and the host sees the same
 * device it saw before the gadget moved to configfs.
 */
static bool setVidPid(uint64_t functions) {
    bool adb = functions & GadgetFunction::ADB;
    const char* pid;

    if (functions & GadgetFunction::RNDIS)
        pid = "0xCF08";
    else if (functions & (GadgetFunction::MTP | GadgetFunction::PTP))
        pid = adb ? "0xCF05" : "0xCF07";
    else
        pid = "0xCF09";

    return WriteStringToFile("0x0955", StringPrintf("%s/idVendor", kGadget)) &&
           WriteStringToFile(pid, StringPrintf("%s/idProduct", kGadget));
}

static const char* configName(uint64_t functions) {
    bool adb = functions & GadgetFunction::ADB;

    if (functions & GadgetFunction::RNDIS) return adb ? "rndis_adb" : "rndis";
    if (functions & GadgetFunction::MTP) return adb ? "mtp_adb" : "mtp";
    if (functions & GadgetFunction::PTP) return adb ? "ptp_adb" : "ptp";
    if (functions & GadgetFunction::ACCESSORY)
        return adb ? "accessory_adb" : "accessory";
    if (functions & GadgetFunction::AUDIO_SOURCE)
        return adb ? "audio_source_adb" : "audio_source";
    return "adb";
}

Status UsbGadget::tearDownGadget() {
    /*
     * The monitor goes first, before anything is unlinked.
     *
     * Unlinking a FunctionFS function unbinds it, which resets the instance,
     * which makes the daemon write its descriptors again -- and the endpoint
     * files reappear within milliseconds. A monitor still running at that
     * moment sees exactly what it is watching for and binds the controller to
     * the configuration being taken apart. The host then asks for a
     * descriptor of a function that is no longer there:
     *
     *     unbind function 'Function FS Gadget'
     *     unbind function 'mtp'
     *     read descriptors
     *     tegra-udc: bind to driver configfs-gadget
     *     Unable to handle kernel NULL pointer dereference
     *     PC is at usb_descriptor_fillbuf
     *
     * Six milliseconds between the unbind and the bind that killed it.
     */
    if (mMonitorCreated) {
        uint64_t flag = kStopMonitor;
        write(mEventFd, &flag, sizeof(flag));
        mMonitor->join();
        mMonitorCreated = false;
    }

    /* Unbinding before touching the configuration is what makes the rest
     * legal: configfs refuses to change a bound gadget. An already unbound one
     * answers ENODEV, which is the state we wanted anyway. */
    WriteStringToFile("none", kUdc);

    if (!WriteStringToFile("0", StringPrintf("%s/bDeviceClass", kGadget)) ||
        !WriteStringToFile("0", StringPrintf("%s/bDeviceSubClass", kGadget)) ||
        !WriteStringToFile("0", StringPrintf("%s/bDeviceProtocol", kGadget)))
        return Status::ERROR;

    if (!unlinkFunctions()) return Status::ERROR;

    /* rndis is the one instance that does not outlive its configuration: it
     * claims a network interface while it exists. */
    std::string rndis = StringPrintf("%s/functions/%s", kGadget, kRndis);
    if (rmdir(rndis.c_str()) && errno != ENOENT)
        PLOG(WARNING) << "cannot remove " << rndis;

    mInotifyFd.reset(-1);
    mEventFd.reset(-1);
    mEpollFd.reset(-1);
    mEndpointList.clear();
    return Status::SUCCESS;
}

static bool addEpollFd(const unique_fd& epfd, const unique_fd& fd) {
    struct epoll_event event;

    event.data.fd = fd;
    event.events = EPOLLIN;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event)) {
        PLOG(ERROR) << "epoll_ctl failed";
        return false;
    }
    return true;
}

Status UsbGadget::setupFunctions(uint64_t functions,
                                 const sp<IUsbGadgetCallback>& callback,
                                 uint64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mLock);
    unique_fd inotifyFd(inotify_init());
    bool ffsEnabled = false;
    int index = 0;

    if (inotifyFd < 0) {
        PLOG(ERROR) << "cannot create an inotify descriptor";
        return Status::ERROR;
    }

    if (functions & GadgetFunction::RNDIS) {
        std::string path = StringPrintf("%s/functions/%s", kGadget, kRndis);
        if (mkdir(path.c_str(), 0770) && errno != EEXIST) {
            PLOG(ERROR) << "cannot create " << path;
            return Status::ERROR;
        }
        if (!linkFunction(kRndis, index++)) return Status::ERROR;
    }
    if ((functions & GadgetFunction::MTP) && !linkFunction(kMtp, index++))
        return Status::ERROR;
    if ((functions & GadgetFunction::PTP) && !linkFunction(kPtp, index++))
        return Status::ERROR;
    if ((functions & GadgetFunction::ACCESSORY) &&
        !linkFunction(kAccessory, index++))
        return Status::ERROR;
    if ((functions & GadgetFunction::AUDIO_SOURCE) &&
        !linkFunction(kAudioSource, index++))
        return Status::ERROR;

    if (functions & GadgetFunction::ADB) {
        ffsEnabled = true;

        if (inotify_add_watch(inotifyFd, kFfsAdbDir, IN_ALL_EVENTS) == -1) {
            PLOG(ERROR) << "cannot watch " << kFfsAdbDir;
            return Status::ERROR;
        }
        if (!linkFunction(kFfsAdb, index++)) return Status::ERROR;

        mEndpointList.push_back(std::string(kFfsAdbDir) + "ep1");
        mEndpointList.push_back(std::string(kFfsAdbDir) + "ep2");
    }

    if (!WriteStringToFile(configName(functions),
                           StringPrintf("%s/strings/0x409/configuration", kConfig)))
        return Status::ERROR;

    /* Nothing here waits on a daemon, so the controller can be written at once
     * and the answer is already known. */
    if (!ffsEnabled) {
        if (!pullUp()) return Status::ERROR;

        mCurrentUsbFunctionsApplied = true;
        if (callback) callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS);
        return Status::SUCCESS;
    }

    unique_fd eventFd(eventfd(0, 0));
    unique_fd epollFd(epoll_create(2));

    if (eventFd < 0 || epollFd < 0) {
        PLOG(ERROR) << "cannot create the monitor's descriptors";
        return Status::ERROR;
    }
    if (!addEpollFd(epollFd, inotifyFd) || !addEpollFd(epollFd, eventFd))
        return Status::ERROR;

    mEpollFd = std::move(epollFd);
    mInotifyFd = std::move(inotifyFd);
    mEventFd = std::move(eventFd);
    gGadgetPullup = false;

    mMonitor = std::make_unique<std::thread>(monitorFfs, this);
    mMonitorCreated = true;

    if (callback) {
        if (!mCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [] { return gGadgetPullup; }))
            LOG(INFO) << "no daemon yet; the monitor goes on waiting for one";

        callback->setCurrentUsbFunctionsCb(
                functions, gGadgetPullup ? Status::SUCCESS : Status::ERROR);
    }

    return Status::SUCCESS;
}

Return<void> UsbGadget::setCurrentUsbFunctions(uint64_t functions,
                                              const sp<IUsbGadgetCallback>& callback,
                                              uint64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(mLockSetCurrentFunction);
    Status status;

    mCurrentUsbFunctions = functions;
    mCurrentUsbFunctionsApplied = false;

    status = tearDownGadget();
    if (status != Status::SUCCESS) goto error;

    /* Leave it down long enough for the host to see it go. */
    usleep(kDisconnectWaitUs);

    if (functions == static_cast<uint64_t>(GadgetFunction::NONE)) {
        if (callback) callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS);
        return Void();
    }

    if (!setVidPid(functions)) {
        status = Status::ERROR;
        goto error;
    }

    status = setupFunctions(functions, callback, timeoutMs);
    if (status != Status::SUCCESS) goto error;

    return Void();

error:
    LOG(ERROR) << "could not set the functions";
    if (callback) callback->setCurrentUsbFunctionsCb(functions, status);
    return Void();
}

Return<void> UsbGadget::getCurrentUsbFunctions(const sp<IUsbGadgetCallback>& callback) {
    if (callback)
        callback->getCurrentUsbFunctionsCb(mCurrentUsbFunctions,
                                          mCurrentUsbFunctionsApplied
                                                  ? Status::FUNCTIONS_APPLIED
                                                  : Status::FUNCTIONS_NOT_APPLIED);
    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
