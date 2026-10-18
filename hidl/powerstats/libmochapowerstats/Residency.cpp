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

#define LOG_TAG "mochapowerstats"

#include <mochapowerstats/Residency.h>

#include <time.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <sstream>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

namespace mocha {
namespace powerstats {

namespace {

constexpr const char* kRailStats = "/sys/power/tegra_rail_stats";
constexpr const char* kGpuRail = "vdd_gpu";
constexpr const char* kSuspends = "/sys/power/suspend_stats/success";

/* tegra_rail_stats counts in clock_t, USER_HZ to the second. */
constexpr int64_t kRailTickMs = 10;

bool ReadInt(const std::string& path, int64_t* value) {
    std::string s;
    if (!android::base::ReadFileToString(path, &s)) return false;
    return sscanf(s.c_str(), "%" SCNd64, value) == 1;
}

std::string CpuidleDir(int cpu) {
    return android::base::StringPrintf("/sys/devices/system/cpu/cpu%d/cpuidle", cpu);
}

std::vector<std::string> CpuidleStates(int cpu) {
    std::vector<std::string> names;
    for (int k = 0;; k++) {
        std::string name;
        if (!android::base::ReadFileToString(
                    android::base::StringPrintf("%s/state%d/name", CpuidleDir(cpu).c_str(), k),
                    &name)) {
            break;
        }
        names.push_back(android::base::Trim(name));
    }
    return names;
}

/* The rail's section of tegra_rail_stats: "<rail> (bin: ...)", then a row
 * of millivolts and time for each level it has been at. 0 mV is gated off. */
bool ReadRail(const std::string& rail, int64_t* offMs, int64_t* onMs) {
    std::string s;
    if (!android::base::ReadFileToString(kRailStats, &s)) return false;
    std::istringstream in(s);
    std::string line;
    bool found = false;
    *offMs = *onMs = 0;
    while (std::getline(in, line)) {
        if (!found) {
            found = android::base::StartsWith(line, rail + " ");
            continue;
        }
        int64_t mv, ticks;
        if (sscanf(line.c_str(), "%" SCNd64 " %" SCNd64, &mv, &ticks) != 2) break;
        (mv == 0 ? *offMs : *onMs) += ticks * kRailTickMs;
    }
    return found;
}

int64_t ElapsedMs(clockid_t clock) {
    timespec t;
    clock_gettime(clock, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

std::vector<State> Numbered(const std::vector<std::string>& names) {
    std::vector<State> states;
    for (size_t i = 0; i < names.size(); i++) states.push_back({int32_t(i), names[i]});
    return states;
}

}  // namespace

void Residencies::Init() {
    /*
     * A core offline now has no cpuidle directory to name its states from.
     * The secondaries all have the same ones (the boot core alone has one
     * more, the memory-clock state), so an offline one borrows the names of
     * a secondary that is online.
     */
    const int cpus = sysconf(_SC_NPROCESSORS_CONF);
    std::vector<std::string> secondary;
    for (int cpu = 1; cpu < cpus && secondary.empty(); cpu++) secondary = CpuidleStates(cpu);
    for (int cpu = 0; cpu < cpus; cpu++) AddCpu(cpu, secondary);
    AddGpu();
    AddSuspend();
    last_.resize(entities_.size());
}

void Residencies::AddCpu(int cpu, const std::vector<std::string>& fallback) {
    std::vector<std::string> names = CpuidleStates(cpu);
    if (names.empty()) names = fallback;
    if (names.empty()) {
        LOG(WARNING) << "cpu" << cpu << " has no cpuidle states";
        return;
    }
    entities_.push_back({int32_t(entities_.size()), "CPU" + std::to_string(cpu), Numbered(names)});
    readers_.push_back([cpu, n = names.size()] {
        std::vector<Residency> r;
        for (size_t k = 0; k < n; k++) {
            const std::string state = CpuidleDir(cpu) + "/state" + std::to_string(k);
            int64_t us, usage;
            if (!ReadInt(state + "/time", &us) || !ReadInt(state + "/usage", &usage)) return r;
            r.push_back({int32_t(k), us / 1000, usage, 0});
        }
        return r;
    });
}

void Residencies::AddGpu() {
    int64_t off, on;
    if (!ReadRail(kGpuRail, &off, &on)) {
        LOG(WARNING) << "no " << kGpuRail << " in " << kRailStats;
        return;
    }
    entities_.push_back({int32_t(entities_.size()), "GPU", Numbered({"off", "on"})});
    readers_.push_back([] {
        std::vector<Residency> r;
        int64_t off, on;
        if (ReadRail(kGpuRail, &off, &on)) r = {{0, off, 0, 0}, {1, on, 0, 0}};
        return r;
    });
}

void Residencies::AddSuspend() {
    entities_.push_back({int32_t(entities_.size()), "SUSPEND", Numbered({"suspended"})});
    readers_.push_back([] {
        int64_t count = 0;
        ReadInt(kSuspends, &count);
        return std::vector<Residency>{
                {0, ElapsedMs(CLOCK_BOOTTIME) - ElapsedMs(CLOCK_MONOTONIC), count, 0}};
    });
}

bool Residencies::Read(int32_t entityId, std::vector<Residency>* out) {
    if (entityId < 0 || size_t(entityId) >= entities_.size()) return false;
    std::vector<Residency> r = readers_[entityId]();
    std::lock_guard<std::mutex> lock(lock_);
    if (r.size() == entities_[entityId].states.size()) {
        last_[entityId] = r;
    }
    *out = last_[entityId];
    return true;
}

}  // namespace powerstats
}  // namespace mocha
