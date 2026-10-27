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

#define LOG_TAG "mochamanager"

#include <mochamanager/Board.h>

#include <dirent.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <iterator>
#include <memory>
#include <sstream>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

namespace mocha {
namespace manager {

namespace {

using android::base::StringPrintf;

constexpr const char* kBattery = "/sys/class/power_supply/battery/";
constexpr const char* kUsbOnline = "/sys/class/power_supply/usb/online";
constexpr const char* kAcOnline = "/sys/class/power_supply/ac/online";
/* "Disconnected" while the input is open, "Connected" otherwise; written
 * "D" and "C". The driver keeps it open through plugging the cable in and
 * out, until it is told otherwise or the tablet restarts. */
constexpr const char* kChargerInput = "/sys/devices/platform/7000c400.i2c/i2c-1/1-006b/input_cable_state";
constexpr const char* kIioDevices = "/sys/bus/iio/devices";
constexpr const char* kCpus = "/sys/devices/system/cpu";
constexpr const char* kCluster = "/sys/kernel/cluster/active";
constexpr const char* kCpuquietEnable = "/sys/devices/system/cpu/cpuquiet/tegra_cpuquiet/enable";
constexpr const char* kHotplugStats = "/sys/kernel/debug/tegra_hotplug/stats";
constexpr const char* kGpuDevfreq = "/sys/class/devfreq/gk20a.0/";
constexpr const char* kGpu = "/sys/devices/platform/gk20a.0/";
constexpr const char* kEmcRate = "/sys/kernel/tegra_emc/emc_rate";
constexpr const char* kThermal = "/sys/class/thermal";
constexpr const char* kZramStat = "/sys/block/zram0/mm_stat";
constexpr const char* kWifiInfo = "/sys/module/bcmdhd/parameters/info_string";

constexpr const char* kChargerProp = "persist.vendor.mocha.charger.disabled";
constexpr const char* kProfileProp = "persist.vendor.mocha.perf.profile";
constexpr const char* kActiveProfileProp = "sys.perf.profile";

std::string ReadLine(const std::string& path) {
    std::string s;
    android::base::ReadFileToString(path, &s);
    return android::base::Trim(s);
}

int64_t ReadInt(const std::string& path, int64_t fallback = 0) {
    int64_t v;
    return sscanf(ReadLine(path).c_str(), "%" SCNd64, &v) == 1 ? v : fallback;
}

/* The "Key:  value kB" lines of /proc/meminfo that are asked for. */
void ReadMeminfo(MemoryStatus* m) {
    std::string s;
    if (!android::base::ReadFileToString("/proc/meminfo", &s)) return;
    std::istringstream in(s);
    std::string key;
    int64_t value;
    while (in >> key >> value) {
        if (key == "MemTotal:") m->totalKb = value;
        else if (key == "MemAvailable:") m->availableKb = value;
        else if (key == "NvMapMemUsed:") m->nvmapUsedKb = value;
        else if (key == "SwapTotal:") m->swapTotalKb = value;
        else if (key == "SwapFree:") m->swapFreeKb = value;
        in.ignore(256, '\n');
    }
}

/* "some avg10=0.00 avg60=..." and the same for full. */
void ReadPressure(MemoryStatus* m) {
    std::string s;
    if (!android::base::ReadFileToString("/proc/pressure/memory", &s)) return;
    for (const auto& line : android::base::Split(s, "\n")) {
        float avg10;
        if (sscanf(line.c_str(), "some avg10=%f", &avg10) == 1) m->psiSome = avg10;
        else if (sscanf(line.c_str(), "full avg10=%f", &avg10) == 1) m->psiFull = avg10;
    }
}

/*
 * cpuquiet's account of the clusters, from its debugfs file:
 *
 *   cpu:            G0         G1         G2         G3         LP
 *   transitions:    7          2          2          4          6
 *   time plugged:   189        46         41         34         5973
 *
 * in clock ticks. G0 is up for as long as the G cluster runs and LP for as
 * long as the LP core does, and every switch is a transition of LP. Only
 * cpuquiet's own switches are counted, and while it is disabled the figures
 * stand still, so there is no account then.
 */
ClusterTime ReadClusterTime() {
    ClusterTime t;
    std::string s;
    if (ReadLine(kCpuquietEnable) != "1" || !android::base::ReadFileToString(kHotplugStats, &s))
        return t;

    std::vector<std::string> cpus, transitions, ticks;
    for (const auto& line : android::base::Split(s, "\n")) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        std::istringstream in(line.substr(colon + 1));
        std::vector<std::string> fields{std::istream_iterator<std::string>(in), {}};
        if (key == "cpu") cpus = std::move(fields);
        else if (key == "transitions") transitions = std::move(fields);
        else if (key == "time plugged") ticks = std::move(fields);
    }
    if (transitions.size() != cpus.size() || ticks.size() != cpus.size()) return t;

    int g = -1, lp = -1;
    for (size_t i = 0; i < cpus.size(); i++) {
        if (cpus[i] == "G0") g = i;
        else if (cpus[i] == "LP") lp = i;
    }
    const long hz = sysconf(_SC_CLK_TCK);
    int64_t gTicks, lpTicks;
    if (g < 0 || lp < 0 || hz <= 0 ||
        !android::base::ParseInt(ticks[g], &gTicks) ||
        !android::base::ParseInt(ticks[lp], &lpTicks) ||
        !android::base::ParseInt(transitions[lp], &t.switches))
        return t;

    t.gMs = gTicks * 1000 / hz;
    t.lpMs = lpTicks * 1000 / hz;
    t.present = true;
    return t;
}

int64_t BootTimeMs() {
    timespec t;
    clock_gettime(CLOCK_BOOTTIME, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

}  // namespace

void Board::Init() {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kIioDevices), closedir);
    if (!dir) return;
    while (dirent* e = readdir(dir.get())) {
        const std::string dev = std::string(kIioDevices) + "/" + e->d_name;
        if (ReadLine(dev + "/name") == "ina230x" && ReadLine(dev + "/rail_name") == "VDD_BAT") {
            ina_ = dev + "/ui_input";
            break;
        }
    }
    if (ina_.empty()) LOG(WARNING) << "no INA on the battery line";

    if (ReadLine(kChargerInput) == "Disconnected" && SetInputOpen(false))
        LOG(INFO) << "closed the charger's input a previous service left open";
}

DeviceInfo Board::ReadDeviceInfo() const {
    DeviceInfo d;
    d.model = android::base::GetProperty("ro.product.model", "");
    d.build = android::base::GetProperty("ro.build.fingerprint", "");
    utsname u;
    if (uname(&u) == 0) d.kernel = std::string(u.release) + " " + u.version;
    MemoryStatus m;
    ReadMeminfo(&m);
    d.memTotalKb = m.totalKb;
    d.wifi = ReadLine(kWifiInfo);
    return d;
}

BatteryInfo Board::ReadBattery() const {
    const std::string b = kBattery;
    BatteryInfo i;
    i.capacityPct = ReadInt(b + "capacity");
    i.status = ReadLine(b + "status");
    i.health = ReadLine(b + "health");
    i.voltageUv = ReadInt(b + "voltage_now");
    i.currentUa = ReadInt(b + "current_now");
    i.tempDeciC = ReadInt(b + "temp");
    i.chargeNowUah = ReadInt(b + "charge_now");
    i.chargeFullUah = ReadInt(b + "charge_full");
    i.chargeFullDesignUah = ReadInt(b + "charge_full_design");
    i.cycleCount = ReadInt(b + "cycle_count");
    i.usbOnline = ReadInt(kUsbOnline) != 0;
    i.acOnline = ReadInt(kAcOnline) != 0;
    i.chargingDisabled = android::base::GetBoolProperty(kChargerProp, false);
    i.inputOpen = ReadLine(kChargerInput) == "Disconnected";
    return i;
}

/*
 * ui_input: one conversion, bus voltage and signed current together, the
 * current negative while the battery discharges.
 */
bool Board::ReadPower(PowerSample* sample) const {
    if (ina_.empty()) return false;
    int mv, ma;
    if (sscanf(ReadLine(ina_).c_str(), "%d %d", &mv, &ma) != 2) return false;
    sample->timestampMs = BootTimeMs();
    sample->voltageMv = mv;
    sample->currentMa = ma;
    sample->powerMw = int(int64_t(mv) * -ma / 1000);
    return true;
}

Snapshot Board::ReadSnapshot() const {
    Snapshot s;
    s.cluster = ReadLine(kCluster);
    s.clusterTime = ReadClusterTime();

    const int cpus = sysconf(_SC_NPROCESSORS_CONF);
    for (int cpu = 0; cpu < cpus; cpu++) {
        const std::string dir = StringPrintf("%s/cpu%d/", kCpus, cpu);
        CpuCore c;
        c.id = cpu;
        /* The boot core cannot be taken offline and has no switch. */
        c.online = access((dir + "online").c_str(), F_OK) != 0 || ReadInt(dir + "online") != 0;
        if (c.online) {
            c.curKhz = ReadInt(dir + "cpufreq/scaling_cur_freq");
            c.minKhz = ReadInt(dir + "cpufreq/scaling_min_freq");
            c.maxKhz = ReadInt(dir + "cpufreq/scaling_max_freq");
            c.governor = ReadLine(dir + "cpufreq/scaling_governor");
        }
        s.cores.push_back(std::move(c));
    }

    s.gpu.curHz = ReadInt(std::string(kGpuDevfreq) + "cur_freq");
    s.gpu.maxHz = ReadInt(std::string(kGpuDevfreq) + "max_freq");
    s.gpu.loadPermille = ReadInt(std::string(kGpu) + "load");
    s.gpu.railgated = ReadLine(std::string(kGpu) + "is_railgated") == "yes";

    s.emcHz = ReadInt(kEmcRate);

    /* Numbered by probe order, which is not the board's to promise: each
     * zone goes by its type. */
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kThermal), closedir);
    while (dir) {
        dirent* e = readdir(dir.get());
        if (!e) break;
        if (!android::base::StartsWith(e->d_name, "thermal_zone")) continue;
        const std::string zone = std::string(kThermal) + "/" + e->d_name + "/";
        const int64_t temp = ReadInt(zone + "temp", INT64_MIN);
        if (temp == INT64_MIN) continue;
        s.thermals.push_back({ReadLine(zone + "type"), int(temp)});
    }

    ReadMeminfo(&s.memory);
    int64_t orig, compr, used;
    if (sscanf(ReadLine(kZramStat).c_str(), "%" SCNd64 " %" SCNd64 " %" SCNd64, &orig, &compr,
               &used) == 3) {
        s.memory.zramOrigBytes = orig;
        s.memory.zramComprBytes = compr;
        s.memory.zramUsedBytes = used;
    }
    ReadPressure(&s.memory);
    return s;
}

bool Board::SetChargingDisabled(bool disabled) {
    return android::base::SetProperty(kChargerProp, disabled ? "1" : "0");
}

bool Board::SetInputOpen(bool open) {
    if (android::base::WriteStringToFile(open ? "D" : "C", kChargerInput)) return true;
    PLOG(ERROR) << "cannot write " << kChargerInput;
    return false;
}

int Board::PerformanceProfile() const {
    return android::base::GetIntProperty(kProfileProp, int(kBalanced));
}

int Board::ActivePerformanceProfile() const {
    return android::base::GetIntProperty(kActiveProfileProp, int(kBalanced));
}

bool Board::SetPerformanceProfile(int profile) {
    if (profile < kPowerSave || profile > kEfficiency) return false;
    return android::base::SetProperty(kProfileProp, std::to_string(profile));
}

}  // namespace manager
}  // namespace mocha
