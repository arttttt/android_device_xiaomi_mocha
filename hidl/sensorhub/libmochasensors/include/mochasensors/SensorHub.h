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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mochasensors/Calibration.h"
#include "mochasensors/Sensor.h"

namespace mocha {
namespace sensors {

/* Where the sensors' events go: from the hub's own thread, and from the
 * caller's for a flush. */
class EventSink {
  public:
    virtual ~EventSink() = default;
    virtual void OnEvents(const std::vector<sensors_event_t>& events) = 0;
};

/*
 * The board's sensors, without an interface on them: the LSM6DB0 hub's,
 * which it reads, fuses and hands over as IIO devices, and the ambient light
 * sensor beside it.
 *
 * This only holds the set of them, waits on their descriptors with one
 * thread and routes the calls a sensors HAL takes -- activate, batch, flush
 * -- to the sensor they are for. The types are libhardware's sensor_t and
 * sensors_event_t, plain C that outlives any IDL; a sensors interface on top
 * only translates.
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

  private:
    Sensor* Find(int handle);
    void Loop();
    /* The accelerometer's calibration file was written. */
    void OnCalibrationFile();
    /* The nine-axis fusion went on putting out NaN: start the hub's
     * magnetometer calibration over with those sensors held off. */
    void ResetFusion();
    void SaveCalibration();

    EventSink* sink_;
    std::vector<std::unique_ptr<Sensor>> sensors_;
    std::vector<sensor_t> list_;
    /* Which sensor a descriptor belongs to. */
    std::map<int, Sensor*> owners_;
    /* Held over every call into a sensor, from either side. */
    std::mutex lock_;
    std::thread thread_;
    HubCalibration calibration_;
    bool haveHub_ = false;
    int64_t lastFaultNs_ = 0;
    /* When what the hub has learnt was last kept, CLOCK_BOOTTIME. */
    int64_t lastSaveNs_ = 0;
    int epoll_ = -1;
    int wake_ = -1;
    int inotify_ = -1;
    std::atomic<bool> stop_{false};
};

}  // namespace sensors
}  // namespace mocha

#endif  // MOCHA_SENSORS_SENSOR_HUB_H
