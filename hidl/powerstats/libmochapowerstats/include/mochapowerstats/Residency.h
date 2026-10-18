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
#ifndef MOCHA_POWERSTATS_RESIDENCY_H
#define MOCHA_POWERSTATS_RESIDENCY_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace mocha {
namespace powerstats {

struct State {
    int32_t id;
    std::string name;
};

/* Something that spends its time in one of a fixed set of states. */
struct Entity {
    int32_t id;
    std::string name;
    std::vector<State> states;
};

struct Residency {
    int32_t stateId;
    int64_t timeMs;
    /* 0 where the kernel does not count entries. */
    int64_t entries;
    /* 0 where the kernel does not say. */
    int64_t lastEntryMs;
};

/*
 * Where the board's time goes, from sysfs alone:
 *
 *   CPU0..3  -- each core's cpuidle states;
 *   GPU      -- its rail on or gated off, out of tegra_rail_stats;
 *   SUSPEND  -- the system suspended, BOOTTIME over MONOTONIC.
 *
 * The set of entities and states is fixed when Init runs: the interface
 * hands it out once and then reports against it.
 */
class Residencies {
  public:
    void Init();
    const std::vector<Entity>& entities() const { return entities_; }
    /* false if there is no entity of that id. */
    bool Read(int32_t entityId, std::vector<Residency>* out);

  private:
    void AddCpu(int cpu, const std::vector<std::string>& fallback);
    void AddGpu();
    void AddSuspend();

    std::vector<Entity> entities_;
    std::vector<std::function<std::vector<Residency>()>> readers_;
    /* A core taken offline loses its cpuidle directory; what it last said
     * stands in until it is back. Indexed as entities_. */
    std::vector<std::vector<Residency>> last_;
    std::mutex lock_;
};

}  // namespace powerstats
}  // namespace mocha

#endif  // MOCHA_POWERSTATS_RESIDENCY_H
