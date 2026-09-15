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

#define LOG_TAG "android.hardware.vibrator@1.0-service.mocha"

#include "Actuator.h"

#include <SysfsNode.h>

#include <cmath>
#include <string>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

static const std::string ENABLE = "/sys/class/timed_output/vibrator/enable";
static const std::string STRENGTH = "/sys/vibrator/pwmvalue";

/* A sequence of strengths and durations, played by the driver rather than by
 * this process. An effect written here keeps its timing whether or not the
 * thread that asked for it is running, which a pair of writes separated by a
 * sleep would not. */
static const std::string PATTERN = "/sys/class/timed_output/vibrator/pattern";

/* What the framework counts strength in. */
static constexpr uint8_t FRAMEWORK_MAX_STRENGTH = 255;

Actuator::Actuator() : mStrengthAdjustable(mocha::sysfs::writable(STRENGTH)) {
    if (!mStrengthAdjustable)
        ALOGW("no %s; effects will play at whatever strength was last set",
              STRENGTH.c_str());
}

bool Actuator::run(uint32_t milliseconds) const {
    return mocha::sysfs::write(ENABLE, static_cast<int>(milliseconds));
}

bool Actuator::stop() const {
    return mocha::sysfs::write(ENABLE, 0);
}

bool Actuator::setStrength(uint8_t amplitude) const {
    /* The interface counts 1 to 255; the amplifier counts 1 to 127. Map the
     * ends onto each other and space the rest evenly between them. */
    long strength = std::lround((amplitude - 1) / (FRAMEWORK_MAX_STRENGTH - 1.0) *
                                        (MAX_STRENGTH - 1) + 1);

    return mocha::sysfs::write(STRENGTH, static_cast<int>(strength));
}

/*
 * The leading byte is the mode the driver reads out of the buffer before the
 * pairs begin; a trailing zero pair ends the sequence. Durations are one byte,
 * so no single step runs longer than 255 ms -- which no effect here wants to.
 */
bool Actuator::play(const std::vector<Step>& steps) const {
    std::vector<uint8_t> buf;

    buf.push_back(0);
    for (const auto& step : steps) {
        buf.push_back(step.first);
        buf.push_back(step.second);
    }
    buf.push_back(0);
    buf.push_back(0);

    return mocha::sysfs::write(PATTERN, buf.data(), buf.size());
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
