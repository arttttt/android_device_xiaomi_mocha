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
 * Android composes a USB gadget in one of two ways, and UsbDeviceManager
 * decides which by asking hwservicemanager for IUsbGadget. Without one it
 * falls back to UsbHandlerLegacy, which writes sys.usb.config and leaves the
 * work to init: the blocks in init.usb.configfs.rc do the symlinking and bind
 * the controller. With one it uses UsbHandlerHal and calls here instead.
 *
 * The reason to prefer this side is not tidiness. The legacy path routes every
 * change through the "none" configuration, and the none block in
 * init.usb.configfs.rc stops adbd unconditionally; the next block starts it
 * again. So adb is torn down and rebuilt on every change of function, even
 * when adb is in both the old set and the new one -- and each restart reopens
 * /dev/usb-ffs/adb/ep0 and rewrites the descriptors. On this kernel that is
 * not free: the release of a dying daemon's last descriptor is deferred to a
 * workqueue, so a new adbd can open ep0 before the old count has been given
 * back, the count never passes through zero, FunctionFS never returns to
 * FFS_READ_DESCRIPTORS, and the write lands on an active instance and fails
 * with ESRCH. Measured at roughly one configuration change in ten.
 *
 * UsbHandlerHal starts adbd when ADB is in the new configuration and stops it
 * only when it is not (UsbDeviceManager.java, setUsbConfig). ctl.start on a
 * running service does nothing, so switching between configurations that both
 * carry adb leaves the daemon alone, and the window closes.
 */

#define LOG_TAG "android.hardware.usb.gadget@1.0-service.mocha"

#include "UsbGadget.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include <cstring>

#include <dirent.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <thread>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

using ::android::base::GetProperty;
using ::android::base::ReadFileToString;
using ::android::base::StringPrintf;
using ::android::base::WriteStringToFile;

static constexpr char kGadget[] = "/config/usb_gadget/g1";
static constexpr char kConfig[] = "/config/usb_gadget/g1/configs/b.1";

/*
 * The instance names are not ours to choose even here. They were fixed when
 * init.tn8.usb.rc created the instances, and the names it uses are the ones
 * init.usb.configfs.rc symlinks, so anything that falls back to the legacy
 * path still finds what it expects. ptp is gs1 rather than gs0 because mtp
 * and ptp are two instances of one driver, numbered in turn.
 */
static constexpr char kFfsAdb[] = "ffs.adb";
static constexpr char kMtp[] = "mtp.gs0";
static constexpr char kPtp[] = "ptp.gs1";
static constexpr char kAccessory[] = "accessory.gs2";
static constexpr char kAudioSource[] = "audio_source.gs3";
static constexpr char kRndis[] = "rndis.gs4";

/* Where init.tn8.usb.rc mounts the FunctionFS instance named adb, and the
 * endpoint file that appears in it once a daemon has described itself. */
static constexpr char kFfsDir[] = "/dev/usb-ffs/adb";
static constexpr char kFfsEp[] = "ep1";

/* How long to wait for adbd to say it has written its descriptors. The
 * framework has already started it by the time it calls us; what is left is
 * the daemon opening ep0 and describing itself, which is milliseconds when it
 * goes well. The ceiling only has to be short enough to leave the caller's
 * own timeout room to report a failure rather than be cut off by it. */
static constexpr int kFfsReadyPollMs = 50;

UsbGadget::UsbGadget() : mCurrentFunctions(static_cast<uint64_t>(GadgetFunction::NONE)),
                         mCurrentApplied(false) {
    mMonitor = std::thread(&UsbGadget::monitorFfs, this);
    mMonitor.detach();
}

static bool write(const std::string& path, const std::string& value) {
    if (!WriteStringToFile(value, path)) {
        PLOG(ERROR) << "cannot write \"" << value << "\" to " << path;
        return false;
    }
    return true;
}

static bool link(const char* function, int index) {
    std::string from = StringPrintf("%s/functions/%s", kGadget, function);
    std::string to = StringPrintf("%s/f%d", kConfig, index);

    if (symlink(from.c_str(), to.c_str())) {
        PLOG(ERROR) << "cannot link " << function << " as f" << index;
        return false;
    }
    return true;
}

/*
 * The product id this board has always reported for each combination. The
 * platform never writes one -- init.usb.configfs.rc leaves whatever was set
 * last -- so it is ours to keep, and the host sees the same device it saw
 * before the gadget moved to configfs.
 */
static const char* productId(uint64_t functions) {
    bool adb = functions & GadgetFunction::ADB;

    if (functions & GadgetFunction::RNDIS) return "0xCF08";
    if (functions & (GadgetFunction::MTP | GadgetFunction::PTP))
        return adb ? "0xCF05" : "0xCF07";
    if (adb) return "0xCF09";
    return "0xCF09";
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

void UsbGadget::tearDown() {
    /*
     * Unbinding first is what makes the rest legal: configfs refuses to change
     * a bound gadget. Writing "none" to a gadget that is already unbound comes
     * back ENODEV, which is not a failure here -- it is the state we wanted.
     */
    WriteStringToFile("none", StringPrintf("%s/UDC", kGadget));

    for (int i = 1; i <= 4; i++) {
        std::string path = StringPrintf("%s/f%d", kConfig, i);
        if (unlink(path.c_str()) && errno != ENOENT)
            PLOG(WARNING) << "cannot unlink " << path;
    }

    /*
     * rndis is the one instance that does not outlive its configuration: it
     * claims a network interface while it exists, and the platform file
     * removes it the same way for the same reason.
     */
    std::string rndis = StringPrintf("%s/functions/%s", kGadget, kRndis);
    if (rmdir(rndis.c_str()) && errno != ENOENT)
        PLOG(WARNING) << "cannot remove " << rndis;

    mCurrentApplied = false;
}

bool UsbGadget::compose(uint64_t functions, uint64_t timeoutMs) {
    int index = 1;

    if (!write(StringPrintf("%s/idProduct", kGadget), productId(functions)))
        return false;

    if (functions & GadgetFunction::RNDIS) {
        std::string path = StringPrintf("%s/functions/%s", kGadget, kRndis);
        if (mkdir(path.c_str(), 0770) && errno != EEXIST) {
            PLOG(ERROR) << "cannot create " << path;
            return false;
        }
        if (!link(kRndis, index++)) return false;
    }
    if (functions & GadgetFunction::MTP && !link(kMtp, index++)) return false;
    if (functions & GadgetFunction::PTP && !link(kPtp, index++)) return false;
    if (functions & GadgetFunction::ACCESSORY && !link(kAccessory, index++)) return false;
    if (functions & GadgetFunction::AUDIO_SOURCE && !link(kAudioSource, index++)) return false;

    if (functions & GadgetFunction::ADB) {
        /*
         * adbd has been started by the caller, and it -- not this service --
         * writes the FunctionFS descriptors. sys.usb.ffs.ready is how it says
         * it has, and linking ffs.adb before then would bind a function that
         * cannot describe itself.
         */
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (GetProperty("sys.usb.ffs.ready", "0") != "1") {
            if (std::chrono::steady_clock::now() >= deadline) {
                LOG(ERROR) << "adbd did not report its descriptors in time";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kFfsReadyPollMs));
        }
        if (!link(kFfsAdb, index++)) return false;
    }

    if (!write(StringPrintf("%s/strings/0x409/configuration", kConfig),
               configName(functions)))
        return false;

    /*
     * The controller is named after the parent device rather than after the
     * device tree node: board-ardbeg.c renames what udc@7d000000 becomes to
     * tegra-udc.0 through OF_DEV_AUXDATA. init.tn8.usb.rc puts that name in
     * sys.usb.controller, and /sys/class/udc is the final word on it.
     */
    std::string udc = GetProperty("sys.usb.controller", "");
    if (udc.empty()) {
        LOG(ERROR) << "sys.usb.controller is not set";
        return false;
    }
    return write(StringPrintf("%s/UDC", kGadget), udc);
}

Return<void> UsbGadget::setCurrentUsbFunctions(uint64_t functions,
                                              const sp<IUsbGadgetCallback>& callback,
                                              uint64_t timeoutMs) {
    std::lock_guard<std::mutex> lock(mLock);

    mCurrentFunctions = functions;
    mCurrentApplied = false;

    tearDown();

    if (functions == static_cast<uint64_t>(GadgetFunction::NONE)) {
        mCurrentApplied = true;
        if (callback) callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS);
        return Void();
    }

    bool ok = compose(functions, timeoutMs);
    mCurrentApplied = ok;

    if (!ok) {
        /* Leave nothing half-built behind: a configuration with some of its
         * functions linked and no controller is worse than none at all,
         * because the next attempt would find f1 taken. */
        tearDown();
    }

    if (callback)
        callback->setCurrentUsbFunctionsCb(functions, ok ? Status::SUCCESS : Status::ERROR);

    return Void();
}

Return<void> UsbGadget::getCurrentUsbFunctions(const sp<IUsbGadgetCallback>& callback) {
    std::lock_guard<std::mutex> lock(mLock);

    if (callback)
        callback->getCurrentUsbFunctionsCb(
                mCurrentFunctions,
                mCurrentApplied ? Status::FUNCTIONS_APPLIED : Status::FUNCTIONS_NOT_APPLIED);
    return Void();
}

void UsbGadget::monitorFfs() {
    int fd = inotify_init1(IN_CLOEXEC);
    if (fd < 0) {
        PLOG(ERROR) << "no inotify descriptor: a daemon that restarts will "
                       "leave the gadget down";
        return;
    }

    if (inotify_add_watch(fd, kFfsDir, IN_CREATE) < 0) {
        PLOG(ERROR) << "cannot watch " << kFfsDir;
        close(fd);
        return;
    }

    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

    while (true) {
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len <= 0) {
            if (errno == EINTR) continue;
            PLOG(ERROR) << "inotify read failed, giving up the watch";
            break;
        }

        bool described = false;
        for (char* p = buf; p < buf + len;) {
            auto* event = reinterpret_cast<struct inotify_event*>(p);
            if ((event->mask & IN_CREATE) && event->len &&
                !strcmp(event->name, kFfsEp))
                described = true;
            p += sizeof(struct inotify_event) + event->len;
        }
        if (!described) continue;

        std::lock_guard<std::mutex> lock(mLock);

        /*
         * Only a configuration that carries adb can be one f_fs took down, and
         * only an unbound gadget needs putting back. Anything else is a daemon
         * arriving during a change the framework asked for, which is already
         * being handled and must not be composed twice.
         */
        if (!(mCurrentFunctions & GadgetFunction::ADB)) continue;

        std::string udc;
        ReadFileToString(StringPrintf("%s/UDC", kGadget), &udc);
        if (!::android::base::Trim(udc).empty()) continue;

        LOG(INFO) << "adbd described itself with the gadget unbound; composing "
                  << "it back";
        tearDown();
        mCurrentApplied = compose(mCurrentFunctions, 1000);
        if (!mCurrentApplied) {
            LOG(ERROR) << "could not compose the gadget back";
            tearDown();
        }
    }

    close(fd);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
