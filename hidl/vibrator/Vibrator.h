/*
 * Copyright (C) 2017 The LineageOS Project
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
#ifndef ANDROID_HARDWARE_VIBRATOR_V1_2_VIBRATOR_H
#define ANDROID_HARDWARE_VIBRATOR_V1_2_VIBRATOR_H

#include <android/hardware/vibrator/1.2/IVibrator.h>
#include <hidl/Status.h>

#include <functional>

#include "Actuator.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_2 {
namespace implementation {

using ::android::hardware::vibrator::V1_0::Effect;
using ::android::hardware::vibrator::V1_0::EffectStrength;
using ::android::hardware::vibrator::V1_0::Status;
using ::android::hardware::vibrator::V1_1::Effect_1_1;

/*
 * The interface, answered over the motor.
 *
 * Effects carry their own strength and are played as patterns, so they
 * neither read nor disturb the amplitude the framework set for ordinary
 * vibration through setAmplitude(). The two are separate settings and were
 * not kept apart before.
 */
class Vibrator : public IVibrator {
  public:
    Return<Status> on(uint32_t timeoutMs) override;
    Return<Status> off() override;
    Return<bool> supportsAmplitudeControl() override;
    Return<Status> setAmplitude(uint8_t amplitude) override;
    Return<void> perform(Effect effect, EffectStrength strength, perform_cb _hidl_cb) override;
    Return<void> perform_1_1(Effect_1_1 effect, EffectStrength strength,
                             perform_1_1_cb _hidl_cb) override;
    Return<void> perform_1_2(V1_2::Effect effect, EffectStrength strength,
                             perform_1_2_cb _hidl_cb) override;

  private:
    /*
     * Both entry points end here.
     *
     * A minor version adds effects without changing what the older ones
     * mean, so its enumeration is the older one widened and every value of
     * the narrow enum is that same value in the wide one. There is one table
     * of effects, and the versioned methods only decide how wide a name they
     * were allowed to be called with.
     */
    Return<void> answerWith(V1_2::Effect effect, EffectStrength strength,
                            const std::function<void(Status, uint32_t)>& reply);

    Actuator mMotor;
};

}  // namespace implementation
}  // namespace V1_2
}  // namespace vibrator
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_VIBRATOR_V1_2_VIBRATOR_H
