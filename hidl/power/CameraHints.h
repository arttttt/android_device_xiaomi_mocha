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
#ifndef MOCHA_POWER_CAMERA_HINTS_H
#define MOCHA_POWER_CAMERA_HINTS_H

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "QosFloor.h"

namespace aidl::android::hardware::power::impl::mocha {

/*
 * What the camera HAL's hints are worth on this board.
 *
 * The camera says three things. That it is opening or about to take a picture
 * (CAMERA_LAUNCH, CAMERA_SHOT): a burst, for as long as it names. That it is
 * streaming at some load (CAMERA_STREAMING_LOW, _MID, _HIGH): held until it
 * says it has stopped. In preview the interactive governor sees a load that
 * comes in short bursts per frame and keeps the clocks low, and the frame
 * misses its time; the answer is a lowest clock, never a highest one.
 *
 * Everything is a PM QoS floor of this service's own (QosFloor), never a cap
 * and never a node anybody else writes. The composer lifts the same CPU and
 * GPU nodes for its own work, and the kernel takes the higher of the two, so
 * neither undoes the other: the composer lifts less while the camera's floor
 * already stands, and as much as it needs again once it goes. The burst has
 * floors of its own beside the streaming ones, so the end of a burst never
 * takes the stream's floor down with it.
 *
 * No floor goes above the performance profile's ceiling: the profile is the
 * user's choice of how hard this device may try, and a hint does not
 * overrule it. Under the battery saver nothing is held at all -- the same
 * answer a touch gets -- but what the camera asked for is remembered, so the
 * saver going off mid-stream puts the stream's floor back.
 *
 * The camera cannot be watched over binder: setMode carries no client to
 * link to death. A camera provider that dies mid-stream is seen by its init
 * state instead (ProviderWatch), which calls reset().
 */
class CameraHints {
  public:
    enum Level { LOW = 0, MID, HIGH, LEVELS };

    CameraHints();
    ~CameraHints();

    CameraHints(const CameraHints&) = delete;
    CameraHints& operator=(const CameraHints&) = delete;

    void stream(Level level, bool on);
    void boost(int32_t durationMs);

    /* The battery saver, or the power save profile, coming on and off. */
    void setSuppressed(bool suppressed);

    /* The camera is gone; nothing it asked for stands. */
    void reset();

  private:
    void applyStreamLocked();
    void dropBoostLocked();
    void timerLoop();

    std::mutex mLock;
    std::condition_variable mWake;
    bool mQuit = false;

    std::array<bool, LEVELS> mStreaming{};
    bool mSuppressed = false;

    bool mBoosting = false;
    std::chrono::steady_clock::time_point mBoostUntil;

    QosFloor mCpu{"/dev/cpu_freq_min"};
    QosFloor mGpu{"/dev/gpu_freq_min"};
    QosFloor mEmc{"/dev/emc_freq_min"};

    QosFloor mBoostCpu{"/dev/cpu_freq_min"};
    QosFloor mBoostGpu{"/dev/gpu_freq_min"};

    /* Last, so it starts once everything it touches exists. */
    std::thread mTimer;
};

}  // namespace aidl::android::hardware::power::impl::mocha

#endif  // MOCHA_POWER_CAMERA_HINTS_H
