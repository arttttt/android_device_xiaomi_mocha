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
#ifndef MOCHA_SENSORS_INPUT_DEVICE_H
#define MOCHA_SENSORS_INPUT_DEVICE_H

#include <linux/input.h>

#include <string>

namespace mocha {
namespace sensors {

/*
 * A sensor that reports through the input layer rather than the hub: the
 * ambient light sensor, drivers/input/misc/isl29035.c. Its events are
 * stamped on CLOCK_BOOTTIME, which evdev takes from this kernel on.
 *
 * The driver keeps the chip powered down until both its "enable" is set and
 * the node is held open, so the node stays open for the life of this.
 */
class InputDevice {
  public:
    ~InputDevice();

    /* The input device whose name is name; false if there is none. */
    bool Open(const std::string& name);

    int fd() const { return fd_; }
    /* The absolute axis' range, as the driver declares it. */
    int maximum() const { return maximum_; }

    bool SetEnabled(bool enabled);

    /* One event; false when there is none. */
    bool Read(input_event* event);

  private:
    std::string sysfs_;
    int fd_ = -1;
    int maximum_ = 0;
    bool enabled_ = false;
};

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_INPUT_DEVICE_H
