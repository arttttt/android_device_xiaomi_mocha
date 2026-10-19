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

/*
 * The hub's sensors straight from libmochasensors, with no interface and no
 * framework: lists them, runs the ones asked for and prints what they give.
 * The sensors HAL in service holds the same IIO devices, so it has to be
 * stopped first.
 *
 *   sensorhub_test                      list
 *   sensorhub_test <seconds> <hz> [handle...]   run (all if none named)
 */

#include <time.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>

#include <mochasensors/SensorHub.h>

using mocha::sensors::EventSink;
using mocha::sensors::SensorHub;

namespace {

int64_t BootNs() {
    timespec now;
    clock_gettime(CLOCK_BOOTTIME, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
}

class Printer : public EventSink {
  public:
    void OnEvents(const std::vector<sensors_event_t>& events) override {
        std::lock_guard<std::mutex> guard(lock_);
        int64_t now = BootNs();
        for (const auto& e : events) {
            if (e.type == SENSOR_TYPE_META_DATA) {
                printf("flush complete %d\n", e.meta_data.sensor);
                continue;
            }
            auto& s = seen_[e.sensor];
            if (s.count == 0) s.first = e.timestamp;
            s.last = e.timestamp;
            s.count++;
            s.lagNs += now - e.timestamp;
            if (s.count % 10 == 1) {
                printf("%2d t=%.3f lag=%3lldms %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f\n", e.sensor,
                       e.timestamp / 1e9, static_cast<long long>((now - e.timestamp) / 1000000),
                       e.data[0], e.data[1], e.data[2], e.data[3], e.data[4], e.data[5]);
            }
        }
    }

    void Summary() {
        std::lock_guard<std::mutex> guard(lock_);
        for (const auto& [handle, s] : seen_) {
            double span = (s.last - s.first) / 1e9;
            printf("handle %2d: %llu events, %.2f Hz, mean lag %.1f ms\n", handle,
                   static_cast<unsigned long long>(s.count), span > 0 ? (s.count - 1) / span : 0.0,
                   s.lagNs / 1e6 / s.count);
        }
    }

  private:
    struct Seen {
        uint64_t count = 0;
        int64_t first = 0, last = 0, lagNs = 0;
    };
    std::mutex lock_;
    std::map<int, Seen> seen_;
};

}  // namespace

int main(int argc, char** argv) {
    Printer printer;
    SensorHub hub(&printer);
    if (!hub.Start()) {
        fprintf(stderr, "no sensors\n");
        return 1;
    }

    for (const auto& s : hub.sensors()) {
        printf("%2d %-28s type %2d range %9.3f res %.6f power %.2f delay %d..%ld us\n", s.handle,
               s.name, s.type, s.maxRange, s.resolution, s.power, s.minDelay, static_cast<long>(s.maxDelay));
    }
    if (argc < 3) return 0;

    int seconds = atoi(argv[1]);
    int64_t period = 1000000000LL / atoi(argv[2]);
    std::vector<int> handles;
    for (int i = 3; i < argc; i++) handles.push_back(atoi(argv[i]));
    if (handles.empty())
        for (const auto& s : hub.sensors()) handles.push_back(s.handle);

    for (int handle : handles) {
        hub.Batch(handle, period, 0);
        int err = hub.Activate(handle, true);
        if (err) fprintf(stderr, "activate %d: %d\n", handle, err);
    }
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    for (int handle : handles) hub.Flush(handle);
    printf("%s", hub.Dump().c_str());
    hub.DeactivateAll();
    printer.Summary();
    return 0;
}
