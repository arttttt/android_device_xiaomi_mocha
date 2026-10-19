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
#ifndef MOCHA_SENSORS_IIO_DEVICE_H
#define MOCHA_SENSORS_IIO_DEVICE_H

#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <string>
#include <vector>

namespace mocha {
namespace sensors {

/* One channel of a scan record, as scan_elements describes it. */
struct IioChannel {
    std::string name;
    int index = 0;
    bool isSigned = false;
    int bits = 0;
    int storageBytes = 0;
    int shift = 0;
    /* Where it sits in a record. */
    size_t offset = 0;
    double scale = 1.0;
};

/*
 * An IIO device of the sensor hub: its scan record, its buffer, and the few
 * knobs the hub's driver gives it.
 *
 * Every write to the hub's sysfs costs 30 ms or more -- the driver raises
 * the hub's wake line and waits for it -- so nothing here is written that
 * already holds the value.
 */
class IioDevice {
  public:
    ~IioDevice();

    /* The device whose name is name; false if there is none. */
    bool Open(const std::string& name);

    const std::string& name() const { return name_; }
    int fd() const { return fd_; }
    size_t recordBytes() const { return recordBytes_; }
    const std::vector<IioChannel>& channels() const { return channels_; }
    /* The channel called name, or nullptr. */
    const IioChannel* Channel(const std::string& name) const;
    /* The rates it can be set to, in Hz, lowest first. */
    const std::vector<int>& rates() const { return rates_; }

    /* A channel's one-off reading through sysfs, as in_<channel>_raw;
     * only while the buffer is off. */
    bool ReadRaw(const std::string& channel, int64_t* value) const;

    bool SetEnabled(bool enabled);
    /* The lowest rate at or above hz, or the highest it has. */
    bool SetRate(int hz);
    int rate() const { return rate_; }

    /* Whole records; 0 when there is nothing, -errno on failure. */
    ssize_t Read(uint8_t* buffer, size_t size);

    /* The device's event channel, where the hub's driver puts what is no
     * sample: a step, a significant motion. -1 on failure. */
    int OpenEvents();
    int eventFd() const { return eventFd_; }
    /* One event's time; false when there is none. */
    bool ReadEvent(int64_t* timestamp);

    /* A channel of a record, sign-extended and shifted, not scaled. */
    static int64_t Raw(const IioChannel& channel, const uint8_t* record);
    /* The same bits, taken as the IEEE float the hub sends them as. */
    static float Float(const IioChannel& channel, const uint8_t* record);

  private:
    std::string Path(const std::string& attribute) const;
    bool ReadChannels();
    bool ReadRates();

    std::string name_;
    std::string sysfs_;
    int number_ = -1;
    int fd_ = -1;
    int eventFd_ = -1;
    bool enabled_ = false;
    int rate_ = 0;
    size_t recordBytes_ = 0;
    std::vector<IioChannel> channels_;
    std::vector<int> rates_;
};

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_IIO_DEVICE_H
