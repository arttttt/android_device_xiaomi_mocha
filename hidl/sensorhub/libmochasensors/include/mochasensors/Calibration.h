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
#ifndef MOCHA_SENSORS_CALIBRATION_H
#define MOCHA_SENSORS_CALIBRATION_H

#include <cstdint>
#include <string>

#include "mochasensors/IioDevice.h"

namespace mocha {
namespace sensors {

/*
 * The accelerometer's calibration, ours: the hub cannot learn it.
 *
 * The offset is the hub's: three counts its firmware subtracts from each axis
 * in Android's axes (hub firmware: out = (raw_y - o0, raw_x - o1, -raw_z -
 * o2)), so everything the hub derives from the accelerometer is corrected
 * too. The scale is what the hub has no way to apply -- this chip reads a
 * part in a hundred high on Z -- and the HAL multiplies by it.
 *
 * Kept as text, "offset x y z" and "scale x y z" lines, so a calibration can
 * be read and written by hand.
 */
struct AccelCalibration {
    int16_t offset[3] = {0, 0, 0};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    /* Whether there is one at all, rather than these defaults. */
    bool present = false;

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};

/*
 * What the hub learns or is told about its sensors, and where it is kept
 * across boots. The hub loses all of it on every boot -- its firmware is
 * loaded into RAM afresh -- so it is written back when the HAL starts.
 *
 * The gyroscope's zero offset and the magnetometer's state the hub learns on
 * its own and hands out on request; they are saved now and then, but never
 * while the nine-axis fusion is putting out NaN: a state that leaves the
 * fusion broken would otherwise come back with every boot.
 */
class HubCalibration {
  public:
    static constexpr const char* kDirectory = "/data/vendor/sensors";
    static constexpr const char* kAccel = "/data/vendor/sensors/accel.cal";
    static constexpr const char* kGyro = "/data/vendor/sensors/gyro.dat";
    static constexpr const char* kMagn = "/data/vendor/sensors/magn.dat";

    /* Finds the hub; false if it is not there. */
    bool Open();

    /* Writes back to the hub what was kept, and loads the accelerometer's. */
    void Restore();
    /* Reads what the hub has learnt and keeps it. */
    void Save();

    /* The accelerometer's, read again from its file and given to the hub. */
    bool ReloadAccel();
    const AccelCalibration& accel() const { return accel_; }

    /* Has the hub start its magnetometer calibration over, which also sets
     * its nine-axis fusion going again after it has put out NaN. */
    bool ResetMagnetometer();

  private:
    bool WriteOffset(const char* attribute, const std::string& bytes, const char* what);
    void Keep(const char* attribute, const char* path);

    IioDevice core_;
    AccelCalibration accel_;
};

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_CALIBRATION_H
