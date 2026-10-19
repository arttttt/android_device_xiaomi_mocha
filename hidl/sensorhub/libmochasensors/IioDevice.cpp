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

#include "mochasensors/IioDevice.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/iio/events.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

namespace mocha {
namespace sensors {

namespace {

constexpr const char* kIioDevices = "/sys/bus/iio/devices/";

bool ReadValue(const std::string& path, std::string* value) {
    if (!android::base::ReadFileToString(path, value)) return false;
    *value = android::base::Trim(*value);
    return true;
}

bool WriteValue(const std::string& path, const std::string& value) {
    if (android::base::WriteStringToFile(value, path)) return true;
    PLOG(ERROR) << "cannot write " << value << " to " << path;
    return false;
}

/*
 * "le:s16/16>>0": endianness, sign, bits, storage bits, shift. A channel of
 * no data has a NUL for its sign -- the hub's driver prints a sign of 0 with
 * %c -- and the record then carries only the time.
 */
bool ParseType(const std::string& type, IioChannel* channel) {
    size_t colon = type.find(':');
    if (colon == std::string::npos || colon + 1 >= type.size()) return false;
    char sign = type[colon + 1];
    channel->isSigned = sign == 's';
    const char* p = type.data() + colon + 1;
    if (sign == 's' || sign == 'u' || sign == '\0') p++;

    char* end;
    long bits = strtol(p, &end, 10);
    if (end == p || *end != '/') return false;
    p = end + 1;
    long storage = strtol(p, &end, 10);
    if (end == p || strncmp(end, ">>", 2) != 0) return false;
    p = end + 2;
    long shift = strtol(p, &end, 10);
    if (end == p) return false;

    channel->bits = static_cast<int>(bits);
    channel->storageBytes = static_cast<int>(storage / 8);
    channel->shift = static_cast<int>(shift);
    return true;
}

}  // namespace

IioDevice::~IioDevice() {
    if (eventFd_ >= 0) close(eventFd_);
    if (fd_ >= 0) {
        if (enabled_) SetEnabled(false);
        close(fd_);
    }
}

std::string IioDevice::Path(const std::string& attribute) const {
    return sysfs_ + "/" + attribute;
}

bool IioDevice::Find(const std::string& name) {
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(kIioDevices), closedir);
    if (!dir) return false;

    while (dirent* entry = readdir(dir.get())) {
        int number;
        if (sscanf(entry->d_name, "iio:device%d", &number) != 1) continue;

        std::string path = std::string(kIioDevices) + entry->d_name;
        std::string value;
        if (!ReadValue(path + "/name", &value) || value != name) continue;

        name_ = name;
        sysfs_ = path;
        number_ = number;
        return true;
    }
    return false;
}

bool IioDevice::Open(const std::string& name) {
    if (!Find(name)) return false;

    /* A buffer left running by whoever had the device before: the scan
     * elements cannot be changed under it. */
    WriteValue(Path("buffer/enable"), "0");

    if (!ReadChannels()) return false;
    ReadRates();
    ReadBatch();

    /* The driver sets each device's own trigger at probe; it is only put
     * back if something changed it. */
    std::string trigger = name_ + "-dev" + std::to_string(number_), current;
    if (!ReadValue(Path("trigger/current_trigger"), &current) || current != trigger)
        WriteValue(Path("trigger/current_trigger"), trigger);

    std::string node = "/dev/iio:device" + std::to_string(number_);
    fd_ = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        PLOG(ERROR) << "cannot open " << node;
        return false;
    }
    return true;
}

/*
 * The record is the enabled channels in index order, each aligned to its own
 * size, the whole padded to the largest. All of them are enabled: the driver
 * packs only the enabled ones, so leaving one off would move the others.
 */
bool IioDevice::ReadChannels() {
    std::string elements = Path("scan_elements");
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(elements.c_str()), closedir);
    if (!dir) return false;

    channels_.clear();
    while (dirent* entry = readdir(dir.get())) {
        std::string file = entry->d_name;
        if (!android::base::EndsWith(file, "_en")) continue;

        IioChannel channel;
        channel.name = file.substr(0, file.size() - 3);

        std::string base = elements + "/" + channel.name, value;
        if (!ReadValue(base + "_index", &value) || !android::base::ParseInt(value, &channel.index))
            return false;
        if (!ReadValue(base + "_type", &value) || !ParseType(value, &channel)) {
            LOG(ERROR) << name_ << ": cannot parse the type of " << channel.name;
            return false;
        }
        WriteValue(base + "_en", "1");

        /* Per channel, or one for the whole type: in_accel_x_scale, in_accel_scale. */
        std::string scale;
        if (ReadValue(Path(channel.name + "_scale"), &scale) ||
            ReadValue(Path(channel.name.substr(0, channel.name.rfind('_')) + "_scale"), &scale)) {
            channel.scale = strtod(scale.c_str(), nullptr);
        }
        channels_.push_back(channel);
    }

    std::sort(channels_.begin(), channels_.end(),
              [](const IioChannel& a, const IioChannel& b) { return a.index < b.index; });

    size_t offset = 0, widest = 1;
    for (auto& channel : channels_) {
        size_t size = channel.storageBytes;
        if (size == 0) continue;
        offset = (offset + size - 1) / size * size;
        channel.offset = offset;
        offset += size;
        widest = std::max(widest, size);
    }
    recordBytes_ = (offset + widest - 1) / widest * widest;
    return recordBytes_ > 0;
}

bool IioDevice::ReadBatch() {
    std::string value;
    if (!ReadValue(Path("batch_mode_available"), &value)) return false;
    batchable_ = value.find("continuos") != std::string::npos;
    if (ReadValue(Path("batch_mode_max_event_count"), &value))
        android::base::ParseInt(value, &fifoMaxEvents_);
    if (ReadValue(Path("batch_mode"), &value)) batching_ = value == "continuos";
    if (ReadValue(Path("batch_mode_timeout"), &value)) android::base::ParseInt(value, &timeoutMs_);
    return true;
}

bool IioDevice::SetBatch(bool batch, int timeoutMs) {
    if (!batchable_) return !batch;
    if (batch == batching_ && (!batch || timeoutMs == timeoutMs_)) return true;

    bool running = enabled_;
    if (running && !SetEnabled(false)) return false;

    bool ok = true;
    if (batch && timeoutMs != timeoutMs_) {
        ok = WriteValue(Path("batch_mode_timeout"), std::to_string(timeoutMs));
        if (ok) timeoutMs_ = timeoutMs;
    }
    /* The hub's driver spells it so. */
    if (ok && batch != batching_) {
        ok = WriteValue(Path("batch_mode"), batch ? "continuos" : "disabled");
        if (ok) {
            batching_ = batch;
            ResendRate();
        }
    }

    if (running) SetEnabled(true);
    return ok;
}

/*
 * The hub goes back to a rate of its own when the batch mode changes, while
 * the driver still holds the old one and skips a write of the same value:
 * another rate is written first, so that the one wanted does go out.
 */
void IioDevice::ResendRate() {
    if (rates_.size() < 2 || rate_ <= 0) return;
    int other = rate_ == rates_.front() ? rates_.back() : rates_.front();
    if (WriteValue(Path("sampling_frequency"), std::to_string(other)))
        WriteValue(Path("sampling_frequency"), std::to_string(rate_));
}

bool IioDevice::FlushHub() {
    static std::string flush = [] {
        IioDevice core;
        return core.Find("lsm6db0") ? core.Path("flush") : std::string();
    }();
    return !flush.empty() && WriteValue(flush, "1");
}

bool IioDevice::ReadRates() {
    std::string value;
    if (!ReadValue(Path("sampling_frequency_available"), &value)) return false;
    for (const auto& word : android::base::Split(value, " ")) {
        int hz;
        if (android::base::ParseInt(word, &hz) && hz > 0) rates_.push_back(hz);
    }
    std::sort(rates_.begin(), rates_.end());
    std::string current;
    if (ReadValue(Path("sampling_frequency"), &current)) android::base::ParseInt(current, &rate_);
    return !rates_.empty();
}

const IioChannel* IioDevice::Channel(const std::string& name) const {
    for (const auto& channel : channels_)
        if (channel.name == name) return &channel;
    return nullptr;
}

bool IioDevice::ReadRaw(const std::string& channel, int64_t* value) const {
    std::string raw;
    return ReadValue(Path(channel + "_raw"), &raw) && android::base::ParseInt(raw, value);
}

bool IioDevice::SetEnabled(bool enabled) {
    if (enabled == enabled_) return true;
    if (!WriteValue(Path("buffer/enable"), enabled ? "1" : "0")) return false;
    enabled_ = enabled;
    return true;
}

bool IioDevice::SetRate(int hz) {
    if (rates_.empty()) return true;
    int chosen = rates_.back();
    for (int rate : rates_) {
        if (rate >= hz) {
            chosen = rate;
            break;
        }
    }
    if (chosen == rate_) return true;
    if (!WriteValue(Path("sampling_frequency"), std::to_string(chosen))) return false;
    rate_ = chosen;
    return true;
}

ssize_t IioDevice::Read(uint8_t* buffer, size_t size) {
    size -= size % recordBytes_;
    ssize_t got = read(fd_, buffer, size);
    if (got < 0) return errno == EAGAIN ? 0 : -errno;
    return got - got % recordBytes_;
}

int IioDevice::OpenEvents() {
    if (eventFd_ >= 0) return eventFd_;
    int fd = -1;
    if (ioctl(fd_, IIO_GET_EVENT_FD_IOCTL, &fd) < 0) {
        PLOG(ERROR) << name_ << ": no event channel";
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    eventFd_ = fd;
    return eventFd_;
}

bool IioDevice::ReadEvent(int64_t* timestamp) {
    iio_event_data event;
    if (read(eventFd_, &event, sizeof(event)) != sizeof(event)) return false;
    *timestamp = event.timestamp;
    return true;
}

int64_t IioDevice::Raw(const IioChannel& channel, const uint8_t* record) {
    uint64_t bits = 0;
    memcpy(&bits, record + channel.offset, channel.storageBytes);
    bits >>= channel.shift;
    if (channel.bits < 64) {
        uint64_t mask = (uint64_t{1} << channel.bits) - 1;
        bits &= mask;
        if (channel.isSigned && (bits >> (channel.bits - 1)) & 1) bits |= ~mask;
    }
    return static_cast<int64_t>(bits);
}

float IioDevice::Float(const IioChannel& channel, const uint8_t* record) {
    uint32_t bits;
    memcpy(&bits, record + channel.offset, sizeof(bits));
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace sensors
}  // namespace mocha
