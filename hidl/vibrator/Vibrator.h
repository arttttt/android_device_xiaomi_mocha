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
#ifndef ANDROID_HARDWARE_VIBRATOR_V1_0_VIBRATOR_H
#define ANDROID_HARDWARE_VIBRATOR_V1_0_VIBRATOR_H

#include <android/hardware/vibrator/1.0/IVibrator.h>
#include <hidl/Status.h>

#include "Actuator.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

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

  private:
    Actuator mMotor;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_VIBRATOR_V1_0_VIBRATOR_H
