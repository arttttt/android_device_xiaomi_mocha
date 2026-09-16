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

#define LOG_TAG "android.hardware.vibrator@1.3-service.mocha"

#include "Vibrator.h"

#include "Effects.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_3 {
namespace implementation {

Return<Status> Vibrator::on(uint32_t timeoutMs) {
    return mMotor.run(timeoutMs) ? Status::OK : Status::UNKNOWN_ERROR;
}

Return<Status> Vibrator::off() {
    return mMotor.stop() ? Status::OK : Status::UNKNOWN_ERROR;
}

Return<bool> Vibrator::supportsAmplitudeControl() {
    /* Answered from what the node did rather than with a constant yes. The
     * framework builds on this: told yes, it stops scaling durations to fake
     * intensity and sets amplitudes instead, and a yes that is not true
     * leaves it doing neither. */
    return mMotor.strengthAdjustable();
}

Return<Status> Vibrator::setAmplitude(uint8_t amplitude) {
    if (amplitude == 0) return Status::BAD_VALUE;

    return mMotor.setStrength(amplitude) ? Status::OK : Status::UNKNOWN_ERROR;
}

Return<void> Vibrator::perform(V1_0::Effect effect, EffectStrength strength,
                               perform_cb _hidl_cb) {
    return answerWith(static_cast<Effect>(effect), strength, _hidl_cb);
}

Return<void> Vibrator::perform_1_1(Effect_1_1 effect, EffectStrength strength,
                                   perform_1_1_cb _hidl_cb) {
    return answerWith(static_cast<Effect>(effect), strength, _hidl_cb);
}

Return<void> Vibrator::perform_1_2(V1_2::Effect effect, EffectStrength strength,
                                   perform_1_2_cb _hidl_cb) {
    return answerWith(static_cast<Effect>(effect), strength, _hidl_cb);
}

Return<void> Vibrator::perform_1_3(Effect effect, EffectStrength strength,
                                   perform_1_3_cb _hidl_cb) {
    return answerWith(effect, strength, _hidl_cb);
}

Return<bool> Vibrator::supportsExternalControl() {
    return false;
}

Return<Status> Vibrator::setExternalControl(bool /* enabled */) {
    /* Never reached while the answer above is no, and answered properly
     * regardless: the interface names this the reply for a device that
     * cannot hand its motor to the audio system. */
    return Status::UNSUPPORTED_OPERATION;
}

Return<void> Vibrator::answerWith(Effect effect, EffectStrength strength,
                                  const std::function<void(Status, uint32_t)>& reply) {
    Effects::Shape shape = Effects::of(effect, strength);

    if (shape.steps.empty()) {
        reply(Status::UNSUPPORTED_OPERATION, 0);
        return Void();
    }

    bool played = mMotor.play(shape.steps);

    reply(played ? Status::OK : Status::UNKNOWN_ERROR, played ? shape.lengthMs : 0);
    return Void();
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
