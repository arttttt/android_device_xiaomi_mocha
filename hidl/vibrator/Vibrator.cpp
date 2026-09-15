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

#define LOG_TAG "android.hardware.vibrator@1.0-service.mocha"

#include "Vibrator.h"

#include "Effects.h"

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
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

Return<void> Vibrator::perform(Effect effect, EffectStrength strength, perform_cb _hidl_cb) {
    Effects::Shape shape = Effects::of(effect, strength);

    if (shape.steps.empty()) {
        _hidl_cb(Status::UNSUPPORTED_OPERATION, 0);
        return Void();
    }

    bool played = mMotor.play(shape.steps);

    _hidl_cb(played ? Status::OK : Status::UNKNOWN_ERROR, played ? shape.lengthMs : 0);
    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
