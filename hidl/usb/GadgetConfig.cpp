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

#include "GadgetConfig.h"

#include <SysfsNode.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>

#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <log/log.h>

namespace android {
namespace hardware {
namespace usb {
namespace gadget {
namespace V1_0 {
namespace implementation {

using ::android::base::GetProperty;
using ::android::base::StringPrintf;

static constexpr char kGadget[] = "/config/usb_gadget/g1";
static constexpr char kConfig[] = "/config/usb_gadget/g1/configs/b.1";
static constexpr char kFfsAdbDir[] = "/dev/usb-ffs/adb/";

/*
 * The instance names are the platform's. A name that does not match is a
 * function that silently never appears.
 *
 * rndis is the one that does not outlive its configuration: it claims a
 * network interface while it exists, so it is made when asked for and removed
 * again afterwards. The rest are made once by init.
 */
static constexpr char kFfsAdb[] = "ffs.adb";
static constexpr char kMtp[] = "mtp.gs0";
static constexpr char kPtp[] = "ptp.gs1";
static constexpr char kAccessory[] = "accessory.gs2";
static constexpr char kAudioSource[] = "audio_source.gs3";
static constexpr char kRndis[] = "rndis.gs4";

bool GadgetConfig::linkOne(const char* function, int index) const {
    std::string from = StringPrintf("%s/functions/%s", kGadget, function);
    std::string to = StringPrintf("%s/f%d", kConfig, index);

    if (symlink(from.c_str(), to.c_str())) {
        ALOGE("cannot link %s as f%d: %s", function, index, strerror(errno));
        return false;
    }
    return true;
}

bool GadgetConfig::createFunction(const char* function) const {
    std::string path = StringPrintf("%s/functions/%s", kGadget, function);

    if (mkdir(path.c_str(), 0770) && errno != EEXIST) {
        ALOGE("cannot create %s: %s", path.c_str(), strerror(errno));
        return false;
    }
    return true;
}

void GadgetConfig::removeFunction(const char* function) const {
    std::string path = StringPrintf("%s/functions/%s", kGadget, function);

    if (rmdir(path.c_str()) && errno != ENOENT)
        ALOGW("cannot remove %s: %s", path.c_str(), strerror(errno));
}

std::vector<std::string> GadgetConfig::link(uint64_t functions) {
    std::vector<std::string> endpoints;
    int index = 0;

    if (functions & GadgetFunction::RNDIS) {
        if (!createFunction(kRndis) || !linkOne(kRndis, index++)) return {};
    }
    if ((functions & GadgetFunction::MTP) && !linkOne(kMtp, index++)) return {};
    if ((functions & GadgetFunction::PTP) && !linkOne(kPtp, index++)) return {};
    if ((functions & GadgetFunction::ACCESSORY) && !linkOne(kAccessory, index++)) return {};
    if ((functions & GadgetFunction::AUDIO_SOURCE) && !linkOne(kAudioSource, index++)) return {};

    if (functions & GadgetFunction::ADB) {
        if (!linkOne(kFfsAdb, index++)) return {};

        endpoints.push_back(std::string(kFfsAdbDir) + "ep1");
        endpoints.push_back(std::string(kFfsAdbDir) + "ep2");
    }

    return endpoints;
}

bool GadgetConfig::unlinkAll() const {
    DIR* dir = opendir(kConfig);
    struct dirent* entry;
    bool ok = true;

    if (!dir) {
        ALOGE("cannot read %s: %s", kConfig, strerror(errno));
        return false;
    }

    /* d_type is not reported in configfs, so the name is what there is to go
     * on -- and every link this service makes is called fN. */
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != 'f' || !isdigit(entry->d_name[1])) continue;

        std::string path = StringPrintf("%s/%s", kConfig, entry->d_name);
        if (unlink(path.c_str())) {
            ALOGE("cannot unlink %s: %s", path.c_str(), strerror(errno));
            ok = false;
        }
    }
    closedir(dir);

    removeFunction(kRndis);
    return ok;
}

/*
 * The product id this board has always reported for each combination. The
 * platform never writes one, so it is ours to keep, and the host sees the same
 * device it saw before the gadget moved to configfs.
 */
bool GadgetConfig::setIdentity(uint64_t functions) const {
    bool adb = functions & GadgetFunction::ADB;
    const char* pid;

    if (functions & GadgetFunction::RNDIS)
        pid = "0xCF08";
    else if (functions & (GadgetFunction::MTP | GadgetFunction::PTP))
        pid = adb ? "0xCF05" : "0xCF07";
    else
        pid = "0xCF09";

    return mocha::sysfs::write(StringPrintf("%s/idVendor", kGadget), "0x0955") &&
           mocha::sysfs::write(StringPrintf("%s/idProduct", kGadget), pid);
}

bool GadgetConfig::setConfigurationName(uint64_t functions) const {
    bool adb = functions & GadgetFunction::ADB;
    const char* name;

    if (functions & GadgetFunction::RNDIS) name = adb ? "rndis_adb" : "rndis";
    else if (functions & GadgetFunction::MTP) name = adb ? "mtp_adb" : "mtp";
    else if (functions & GadgetFunction::PTP) name = adb ? "ptp_adb" : "ptp";
    else if (functions & GadgetFunction::ACCESSORY) name = adb ? "accessory_adb" : "accessory";
    else if (functions & GadgetFunction::AUDIO_SOURCE) name = adb ? "audio_source_adb" : "audio_source";
    else name = "adb";

    return mocha::sysfs::write(
            StringPrintf("%s/strings/0x409/configuration", kConfig), name);
}

/*
 * udc-core names the entry in /sys/class/udc after the parent device, and
 * board-ardbeg.c gives the device probed from udc@7d000000 the legacy name
 * tegra-udc.0 through OF_DEV_AUXDATA. init.tn8.usb.rc puts that name in the
 * property; the name the device tree node alone would imply matches nothing,
 * and writing it answers ENODEV.
 */
bool GadgetConfig::bind() const {
    std::string controller = GetProperty("sys.usb.controller", "");

    if (controller.empty()) {
        ALOGE("sys.usb.controller is not set");
        return false;
    }

    return mocha::sysfs::write(StringPrintf("%s/UDC", kGadget), controller);
}

void GadgetConfig::unbind() const {
    /* An already unbound gadget answers ENODEV, which is the state that was
     * wanted anyway -- so this is not worth a failure. */
    mocha::sysfs::write(StringPrintf("%s/UDC", kGadget), "none");
}

void GadgetConfig::resetDeviceClass() const {
    mocha::sysfs::write(StringPrintf("%s/bDeviceClass", kGadget), 0);
    mocha::sysfs::write(StringPrintf("%s/bDeviceSubClass", kGadget), 0);
    mocha::sysfs::write(StringPrintf("%s/bDeviceProtocol", kGadget), 0);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
