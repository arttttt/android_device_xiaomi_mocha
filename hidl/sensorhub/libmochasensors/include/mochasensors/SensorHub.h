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
#ifndef MOCHA_SENSORS_SENSOR_HUB_H
#define MOCHA_SENSORS_SENSOR_HUB_H

#include <hardware/sensors.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mochasensors/IioDevice.h"

namespace mocha {
namespace sensors {

/* Where the hub's events go. Called from the hub's own thread. */
class EventSink {
  public:
    virtual ~EventSink() = default;
    virtual void OnEvents(const std::vector<sensors_event_t>& events) = 0;
};

/*
 * The sensors of the LSM6DB0 hub, without an interface on them.
 *
 * The hub reads its chips, fuses them and hands the results over as IIO
 * devices; this keeps those devices, turns their records into sensor events
 * in Android's units and Android's clock, and takes the calls a sensors HAL
 * takes -- activate, batch, flush -- in the shapes of libhardware's sensor_t
 * and sensors_event_t, which are plain C and outlive any IDL. A sensors
 * interface on top only translates.
 */
class SensorHub {
  public:
    explicit SensorHub(EventSink* sink);
    ~SensorHub();

    /* Opens what the board has and starts reading; false if none. */
    bool Start();

    /* Handles from 1 up, in the order listed. */
    const std::vector<sensor_t>& sensors() const { return list_; }

    /* 0 or -errno, as libhardware's sensors calls. */
    int Activate(int handle, bool enabled);
    int Batch(int handle, int64_t periodNs, int64_t latencyNs);
    int Flush(int handle);
    /* Every sensor off, as after a restart of the framework. */
    void DeactivateAll();

    std::string Dump();

    struct Spec;

  private:
    struct Sensor {
        const Spec* spec;
        sensor_t info;
        std::unique_ptr<IioDevice> device;
        bool active = false;
        int64_t periodNs = 0;
        uint64_t events = 0;
    };

    Sensor* Find(int handle);
    void Loop();
    void Convert(const Sensor& sensor, const uint8_t* record, sensors_event_t* event) const;

    EventSink* sink_;
    std::vector<Sensor> sensors_;
    std::vector<sensor_t> list_;
    std::mutex lock_;
    std::thread thread_;
    int epoll_ = -1;
    int wake_ = -1;
    std::atomic<bool> stop_{false};
};

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_SENSOR_HUB_H
