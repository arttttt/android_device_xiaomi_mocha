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

#define LOG_TAG "mochasensors"

#include "mochasensors/InputDevice.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <memory>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace mocha {
namespace sensors {

namespace {

constexpr const char* kInputClass = "/sys/class/input/";

}  // namespace

InputDevice::~InputDevice() {
    if (fd_ >= 0) {
        if (enabled_) SetEnabled(false);
        close(fd_);
    }
}

bool InputDevice::Open(const std::string& name) {
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(kInputClass), closedir);
    if (!dir) return false;

    std::string found;
    while (dirent* entry = readdir(dir.get())) {
        int number;
        if (sscanf(entry->d_name, "input%d", &number) != 1) continue;

        std::string path = std::string(kInputClass) + entry->d_name, value;
        if (!android::base::ReadFileToString(path + "/name", &value)) continue;
        if (android::base::Trim(value) != name) continue;
        found = path;
        break;
    }
    if (found.empty()) return false;
    sysfs_ = found;

    /* The event node is the eventN under the device. */
    std::unique_ptr<DIR, int (*)(DIR*)> nodes(opendir(sysfs_.c_str()), closedir);
    if (!nodes) return false;
    std::string node;
    while (dirent* entry = readdir(nodes.get())) {
        if (android::base::StartsWith(entry->d_name, "event")) {
            node = std::string("/dev/input/") + entry->d_name;
            break;
        }
    }
    if (node.empty()) return false;

    fd_ = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        PLOG(ERROR) << "cannot open " << node;
        return false;
    }

    int clock = CLOCK_BOOTTIME;
    if (ioctl(fd_, EVIOCSCLOCKID, &clock) < 0)
        PLOG(WARNING) << node << ": events stay on the wall clock";

    input_absinfo info = {};
    if (ioctl(fd_, EVIOCGABS(ABS_MISC), &info) == 0) maximum_ = info.maximum;
    return true;
}

bool InputDevice::SetEnabled(bool enabled) {
    if (enabled == enabled_) return true;
    if (!android::base::WriteStringToFile(enabled ? "1" : "0", sysfs_ + "/enable")) {
        PLOG(ERROR) << "cannot write " << sysfs_ << "/enable";
        return false;
    }
    enabled_ = enabled;
    return true;
}

bool InputDevice::Read(input_event* event) {
    return read(fd_, event, sizeof(*event)) == sizeof(*event);
}

}  // namespace sensors
}  // namespace mocha
