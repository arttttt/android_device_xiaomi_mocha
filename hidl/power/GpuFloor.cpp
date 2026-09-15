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

#define LOG_TAG "android.hardware.power@1.3-service.mocha"

#include "GpuFloor.h"

#include <SysfsNode.h>

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

static const std::string NODE = "/sys/kernel/tegra_gpu/gpu_floor_rate";

/*
 * 72 MHz is the bottom of gpu_available_rates and where the clock sits
 * whenever nothing is asking for the GPU -- measured here, 57 samples out of
 * 60 while an application was being launched. The same measurement is where
 * 252 MHz comes from: it is the step the governor reaches by itself when
 * something does happen.
 *
 * It is a choice, not a measurement of the scene it is for. Forcing client
 * composition to measure that properly needs SurfaceFlinger's debug
 * transaction, which answers only to uid system, and root is not it. If a
 * scene that does fall to the GPU ever turns up, sample gpu_rate during it and
 * let the number settle the argument.
 */
static const std::string IDLE = "72000000";
static const std::string RENDERING = "252000000";

GpuFloor::GpuFloor() {
    mocha::sysfs::write(NODE, IDLE);
}

void GpuFloor::hold() {
    std::lock_guard<std::mutex> lock(mLock);

    mocha::sysfs::write(NODE, RENDERING);
    mHeld = true;
}

void GpuFloor::release() {
    std::lock_guard<std::mutex> lock(mLock);

    releaseLocked();
}

/* Letting go is idempotent, so that a stray "no longer expensive" cannot
 * lower a floor nobody raised. */
void GpuFloor::releaseLocked() {
    if (!mHeld) return;

    mocha::sysfs::write(NODE, IDLE);
    mHeld = false;
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android
