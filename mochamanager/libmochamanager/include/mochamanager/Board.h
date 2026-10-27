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
#ifndef MOCHA_MANAGER_BOARD_H
#define MOCHA_MANAGER_BOARD_H

#include <cstdint>
#include <string>
#include <vector>

namespace mocha {
namespace manager {

/* The same fields as the interface's parcelables; see the .aidl files for
 * what each means. */

struct DeviceInfo {
    std::string model;
    std::string build;
    std::string kernel;
    int64_t memTotalKb = 0;
    std::string wifi;
};

struct BatteryInfo {
    int capacityPct = 0;
    std::string status;
    std::string health;
    int voltageUv = 0;
    int currentUa = 0;
    int tempDeciC = 0;
    int chargeNowUah = 0;
    int chargeFullUah = 0;
    int chargeFullDesignUah = 0;
    int cycleCount = 0;
    bool usbOnline = false;
    bool acOnline = false;
    bool chargingDisabled = false;
    bool inputOpen = false;
};

struct PowerSample {
    int64_t timestampMs = 0;
    int voltageMv = 0;
    int currentMa = 0;
    int powerMw = 0;
};

struct CpuCore {
    int id = 0;
    bool online = false;
    int curKhz = 0;
    int minKhz = 0;
    int maxKhz = 0;
    std::string governor;
};

struct GpuStatus {
    int64_t curHz = 0;
    int64_t maxHz = 0;
    int loadPermille = 0;
    bool railgated = false;
};

struct ThermalZone {
    std::string name;
    int milliCelsius = 0;
};

struct MemoryStatus {
    int64_t totalKb = 0;
    int64_t availableKb = 0;
    int64_t nvmapUsedKb = 0;
    int64_t swapTotalKb = 0;
    int64_t swapFreeKb = 0;
    int64_t zramOrigBytes = 0;
    int64_t zramComprBytes = 0;
    int64_t zramUsedBytes = 0;
    float psiSome = 0;
    float psiFull = 0;
};

struct ClusterTime {
    bool present = false;
    int64_t gMs = 0;
    int64_t lpMs = 0;
    int switches = 0;
};

struct Snapshot {
    std::string cluster;
    std::vector<CpuCore> cores;
    GpuStatus gpu;
    int64_t emcHz = 0;
    std::vector<ThermalZone> thermals;
    MemoryStatus memory;
    ClusterTime clusterTime;
};

enum Profile {
    kPowerSave = 0,
    kBalanced = 1,
    kPerformance = 2,
    kEfficiency = 3,
};

/*
 * The tablet as sysfs, procfs and its properties describe it, and the two
 * switches the quick settings tiles have. Knows no IDL: the service
 * translates.
 */
class Board {
  public:
    /* Finds what moves between boots -- the INA's IIO device -- once, and
     * closes the charger's input if a service before this one left it
     * open: running on the battery is never kept. */
    void Init();

    DeviceInfo ReadDeviceInfo() const;
    BatteryInfo ReadBattery() const;
    /* false if the board has no INA on the battery line. */
    bool ReadPower(PowerSample* sample) const;
    Snapshot ReadSnapshot() const;

    /* The switches as properties; init turns them into the charger's and
     * the governors' settings. */
    bool SetChargingDisabled(bool disabled);
    /* Opens or closes the charger's input (HIZ). */
    bool SetInputOpen(bool open);
    int PerformanceProfile() const;
    int ActivePerformanceProfile() const;
    /* false for a profile that is not one. */
    bool SetPerformanceProfile(int profile);

  private:
    std::string ina_;
};

}  // namespace manager
}  // namespace mocha

#endif  // MOCHA_MANAGER_BOARD_H
