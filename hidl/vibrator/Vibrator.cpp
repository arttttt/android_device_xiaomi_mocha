/*
 * Copyright (C) 2017 The LineageOS Project
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

#define LOG_TAG "VibratorService"

#include <log/log.h>

#include "Vibrator.h"

#include <cmath>
#include <fstream>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

static constexpr int MAX_INTENSITY = 127;
static constexpr int MIN_INTENSITY = 1;

static const char *ENABLE_PATH = "/sys/class/timed_output/vibrator/enable";
static const char *AMPLITUDE_PATH = "/sys/vibrator/pwmvalue";

#define CLICK_TIMING_MS 20

Vibrator::Vibrator() :
        mEnable(ENABLE_PATH),
        mAmplitude(AMPLITUDE_PATH) {}

// Methods from ::android::hardware::vibrator::V1_0::IVibrator follow.
Return<Status> Vibrator::on(uint32_t timeout_ms) {
    mEnable << timeout_ms << std::endl;
    if (!mEnable) {
        ALOGE("Failed to turn vibrator on (%d): %s", errno, strerror(errno));
        return Status::UNKNOWN_ERROR;
    }
    return Status::OK;
}

Return<Status> Vibrator::off()  {
    mEnable << 0 << std::endl;
    if (!mEnable) {
        ALOGE("Failed to turn vibrator off (%d): %s", errno, strerror(errno));
        return Status::UNKNOWN_ERROR;
    }
    return Status::OK;
}

Return<bool> Vibrator::supportsAmplitudeControl()  {
    return true;
}

Return<Status> Vibrator::setAmplitude(uint8_t amplitude) {
    if (amplitude == 0) {
        return Status::BAD_VALUE;
    }
    // Scale the intensity such that an amplitude of 1 is MIN_INTENSITY, an amplitude of 255 is
    // MAX_INTENSITY, and there are equal steps for every value in between.
    long intensity =
            std::lround((amplitude - 1) / 254.0 * (MAX_INTENSITY - MIN_INTENSITY) + MIN_INTENSITY);
    ALOGI("Setting amplitude to: %ld", intensity);
    mAmplitude << intensity << std::endl;
    if (!mAmplitude) {
        ALOGE("Failed to set amplitude (%d): %s", errno, strerror(errno));
        return Status::UNKNOWN_ERROR;
    }
    return Status::OK;
}

Return<void> Vibrator::perform(Effect effect, EffectStrength strength, perform_cb _hidl_cb) {
    if (effect == Effect::CLICK) {
/*        uint8_t amplitude;
        switch (strength) {
        case EffectStrength::LIGHT:
            amplitude = 65;
            break;
        case EffectStrength::MEDIUM:
            amplitude = 131;
            break;
        case EffectStrength::STRONG:
            amplitude = 255;
            break;
        default:
            _hidl_cb(Status::UNSUPPORTED_OPERATION, 0);
            return Void();
        }*/
        on(CLICK_TIMING_MS);
        //setAmplitude(amplitude);
        _hidl_cb(Status::OK, CLICK_TIMING_MS);
    } else {
        _hidl_cb(Status::UNSUPPORTED_OPERATION, 0);
    }
    return Void();
}

} // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
