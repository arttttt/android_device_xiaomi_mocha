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
#ifndef MOCHA_POWER_GPU_FLOOR_H
#define MOCHA_POWER_GPU_FLOOR_H

#include <mutex>

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

/*
 * The lowest rate the GPU is allowed to fall to.
 *
 * Held up while SurfaceFlinger says the composition has gone to the GPU and
 * will stay there, and put back when it has not. The governor already reaches
 * the raised rate by itself when something happens; what it gets wrong is
 * letting the clock drop to the bottom between frames, and that is all this
 * changes.
 */
class GpuFloor {
  public:
    /* Put the floor back wherever it was left. Called at startup, because a
     * service that died with the floor raised leaves it raised: the node
     * outlives the process, and the cost is battery spent on nothing until
     * the next reboot, with nothing in any log to say why. A destructor
     * answers only the orderly exits, and those are not the ones that worry
     * me. */
    GpuFloor();

    void hold();
    void release();

  private:
    void releaseLocked();

    std::mutex mLock;
    bool mHeld = false;
};

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android

#endif  // MOCHA_POWER_GPU_FLOOR_H
