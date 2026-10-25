/*
 * Copyright (C) 2018 The LineageOS Project
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
#ifndef MOCHA_POWER_POWER_H
#define MOCHA_POWER_POWER_H

#include <aidl/android/hardware/power/BnPower.h>

#include <mutex>

#include "BoostPulse.h"
#include "CameraHints.h"
#include "GpuFloor.h"
#include "Profiles.h"
#include "ProviderWatch.h"

namespace aidl::android::hardware::power::impl::mocha {

class Power : public BnPower {
  public:
    Power();

    ndk::ScopedAStatus setMode(Mode type, bool enabled) override;
    ndk::ScopedAStatus isModeSupported(Mode type, bool* _aidl_return) override;
    ndk::ScopedAStatus setBoost(Boost type, int32_t durationMs) override;
    ndk::ScopedAStatus isBoostSupported(Boost type, bool* _aidl_return) override;
    ndk::ScopedAStatus createHintSession(int32_t tgid, int32_t uid,
                                         const std::vector<int32_t>& threadIds,
                                         int64_t durationNanos,
                                         std::shared_ptr<IPowerHintSession>* _aidl_return) override;
    ndk::ScopedAStatus getHintSessionPreferredRate(int64_t* outNanoseconds) override;

  private:
    /* What a hint means on this board. */
    void setInteractive(bool interactive);
    void handleExpensiveRendering(bool expensive);
    void handleLowPower(bool on);
    void hurry();
    void streamCamera(CameraHints::Level level, bool on);

    /* Whether the user has asked this device to spend less: the battery
     * saver, or the power save profile chosen outright. */
    bool sparingLocked() const;

    BoostPulse mBoost;
    GpuFloor mGpuFloor;
    Profiles mProfiles;
    CameraHints mCamera;
    ProviderWatch mProvider{[this] { mCamera.reset(); }};

    /* Holds a whole decision together. The pieces each look after their own
     * state, but a decision made of two of them -- the battery saver coming
     * on and the floors going down with it -- has to be one thing or the
     * other, not half of each when two hints cross. */
    std::mutex mPolicyLock;
    bool mLowPower = false;
};

}  // namespace aidl::android::hardware::power::impl::mocha

#endif  // MOCHA_POWER_POWER_H
