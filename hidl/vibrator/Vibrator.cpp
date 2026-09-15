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

#include <SysfsNode.h>

#include <cmath>
#include <utility>
#include <vector>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

/* How long the motor runs, in milliseconds, and how hard. */
static const std::string ENABLE_PATH = "/sys/class/timed_output/vibrator/enable";
static const std::string AMPLITUDE_PATH = "/sys/vibrator/pwmvalue";

/* A sequence of strengths and durations, played by the driver rather than by
 * this process. An effect written here keeps its timing whether or not the
 * thread that asked for it is running, which a pair of writes separated by a
 * sleep would not. */
static const std::string PATTERN_PATH = "/sys/class/timed_output/vibrator/pattern";

/* The strength the actuator ends at.
 *
 * Not a choice: the amplifier takes a signed real-time playback value and its
 * input is configured bidirectional, so 0x7F is 100% of rated voltage and
 * there is nothing above it to ask for. The driver clamps to the same number. */
static const uint8_t MAX_STRENGTH = 127;

/* What the three strengths the framework knows come out as.
 *
 * Chosen, not measured -- measuring would want an accelerometer against the
 * back of the tablet and something to compare it to, and we have neither.
 * Even spacing of the voltage is not even spacing of the sensation, since
 * perceived intensity grows more slowly than amplitude, so these lean low
 * rather than sitting at a third and two thirds. If they feel wrong in the
 * hand, the hand is the better instrument and these are one line each. */
static const uint8_t STRENGTH_LIGHT = 38;
static const uint8_t STRENGTH_MEDIUM = 76;
static const uint8_t STRENGTH_STRONG = MAX_STRENGTH;

/* How long one click runs, and how long the silence inside a double one.
 *
 * Both were found by hand on the device. Below about 20 ms a click does not
 * become crisper, it becomes weaker: the mass of a linear actuator needs that
 * long to reach speed, and a pulse that ends first is one the finger barely
 * feels. 12 and 15 ms were tried and rejected for exactly that.
 *
 * The gap is what makes two clicks read as one gesture rather than two
 * events. Too short and they blur into a rattle; too long and they are
 * separate taps. */
static const uint32_t CLICK_MS = 20;
static const uint32_t DOUBLE_CLICK_GAP_MS = 60;

static uint8_t strengthOf(EffectStrength strength) {
    switch (strength) {
        case EffectStrength::LIGHT:  return STRENGTH_LIGHT;
        case EffectStrength::MEDIUM: return STRENGTH_MEDIUM;
        case EffectStrength::STRONG: return STRENGTH_STRONG;
    }
    return STRENGTH_MEDIUM;
}

/* Play a sequence of (strength, milliseconds) pairs.
 *
 * The leading byte is the mode the driver reads out of the buffer before the
 * pairs begin; a trailing zero pair ends the sequence. Durations are one byte,
 * so no single step runs longer than 255 ms -- which no effect here wants to.
 */
static bool playPattern(const std::vector<std::pair<uint8_t, uint8_t>> &steps) {
    std::vector<uint8_t> buf;

    buf.push_back(0);
    for (const auto &step : steps) {
        buf.push_back(step.first);
        buf.push_back(step.second);
    }
    buf.push_back(0);
    buf.push_back(0);

    return mocha::sysfs::write(PATTERN_PATH, buf.data(), buf.size());
}

Vibrator::Vibrator() : mAmplitudeControl(mocha::sysfs::writable(AMPLITUDE_PATH)) {
    /* A node that is missing, or that init has not handed over, says so here
     * rather than on the first effect. */
    if (!mAmplitudeControl) {
        ALOGW("no %s; effects will play at whatever strength was last set",
              AMPLITUDE_PATH.c_str());
    }
}

Return<Status> Vibrator::on(uint32_t timeoutMs) {
    return mocha::sysfs::write(ENABLE_PATH, timeoutMs) ? Status::OK : Status::UNKNOWN_ERROR;
}

Return<Status> Vibrator::off() {
    return mocha::sysfs::write(ENABLE_PATH, 0) ? Status::OK : Status::UNKNOWN_ERROR;
}

Return<bool> Vibrator::supportsAmplitudeControl() {
    /* Answered from what the node did rather than with a constant yes. The
     * framework builds on this: told yes, it stops scaling durations to fake
     * intensity and sets amplitudes instead, and a yes that is not true
     * leaves it doing neither. */
    return mAmplitudeControl;
}

Return<Status> Vibrator::setAmplitude(uint8_t amplitude) {
    if (amplitude == 0) {
        return Status::BAD_VALUE;
    }

    /* The interface counts 1 to 255; the amplifier counts 1 to 127. Map the
     * ends onto each other and space the rest evenly between them. */
    long strength = std::lround((amplitude - 1) / 254.0 * (MAX_STRENGTH - 1) + 1);

    return mocha::sysfs::write(AMPLITUDE_PATH, strength) ? Status::OK : Status::UNKNOWN_ERROR;
}

Return<void> Vibrator::perform(Effect effect, EffectStrength strength, perform_cb _hidl_cb) {
    uint8_t amplitude = strengthOf(strength);
    uint32_t lengthMs;
    bool played;

    /* Effects carry their own strength and are played as a pattern, so they
     * neither read nor disturb the amplitude the framework has set for
     * ordinary vibration through setAmplitude(). The two are separate
     * settings and were not kept apart before: perform() called
     * setAmplitude() and left the framework's choice replaced. */
    switch (effect) {
        case Effect::CLICK:
            lengthMs = CLICK_MS;
            played = playPattern({{amplitude, CLICK_MS}});
            break;

        case Effect::DOUBLE_CLICK:
            lengthMs = CLICK_MS + DOUBLE_CLICK_GAP_MS + CLICK_MS;
            played = playPattern({{amplitude, CLICK_MS},
                                  {0, DOUBLE_CLICK_GAP_MS},
                                  {amplitude, CLICK_MS}});
            break;

        default:
            _hidl_cb(Status::UNSUPPORTED_OPERATION, 0);
            return Void();
    }

    /* The length given back is what the caller schedules against, so it is
     * the length of the whole effect -- silence in the middle included. */
    _hidl_cb(played ? Status::OK : Status::UNKNOWN_ERROR, played ? lengthMs : 0);

    return Void();
}

} // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
