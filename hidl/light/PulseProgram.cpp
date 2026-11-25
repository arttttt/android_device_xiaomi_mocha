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

#include "PulseProgram.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

namespace {

/* The engine's two clocks: the 32 kHz master divided by 16 or by 512. */
constexpr double kFastCycleMs = 1000.0 * 16 / 32768;   /* 0.49 ms */
constexpr double kSlowCycleMs = 1000.0 * 512 / 32768;  /* 15.6 ms */

/* A ramp step lasts up to 63 cycles, and one instruction ramps by up to
 * 128 steps -- half the scale, so a climb to full brightness takes two. */
constexpr int kMaxCycles = 63;
constexpr int kMaxSteps = 128;

/* The encodings are the datasheet's (SNVS441I, tables 14 to 19).
 *
 * Ramp: 0, bit 14 for the slow clock, six bits of cycles per step, a sign
 * bit for downwards, and seven bits of steps less one. With those seven at
 * zero it moves nothing and is a wait of one step. */
uint16_t ramp(bool slow, int cycles, bool down, int steps) {
    return (slow ? 0x4000 : 0) | (cycles << 8) | (down ? 0x80 : 0) | (steps - 1);
}

/* Set PWM: what a ramp with no cycles at all would look like. */
uint16_t setPwm(uint8_t value) {
    return 0x4000 | value;
}

/* Trigger: 111, then six bits of channels to wait for and six of channels
 * to send to, bit 0 red, 1 green, 2 blue. A trigger that arrives is kept
 * until the channel waits for it, so sending first and waiting second
 * cannot miss one. */
uint16_t sendTo(uint8_t channels) {
    return 0xe000 | (channels << 1);
}

uint16_t waitFor(uint8_t channels) {
    return 0xe000 | (channels << 7);
}

/* The pause at the top and the bottom of a breath: two slow cycles, 31 ms,
 * the firmware's own. */
constexpr int kPauseCycles = 2;
constexpr double kPauseMs = kPauseCycles * kSlowCycleMs;

uint16_t pause() {
    return ramp(true, kPauseCycles, false, 1);
}

/* From dark to the brightness or back, a step per unit, over about ms. The
 * step is as long as ms divided among the units, on whichever clock holds
 * it; a very dim channel climbs in less time than asked for, being unable
 * to take more than a second a step, and waits for the others at the top
 * of the next breath.
 *
 * Past 128 units the climb is two instructions, split evenly: a ramp of a
 * single step cannot be written, its encoding being the wait's. For the
 * same reason a channel at a brightness of one climbs to two. */
void appendRamp(std::vector<uint16_t>& program, uint8_t brightness, double ms, bool down) {
    if (brightness == 0) return;

    int units = std::max<int>(brightness, 2);
    int parts = (units + kMaxSteps - 1) / kMaxSteps;

    double stepMs = ms / units;
    bool slow = stepMs > kMaxCycles * kFastCycleMs;
    double cycle = slow ? kSlowCycleMs : kFastCycleMs;
    int cycles = std::clamp(static_cast<int>(std::lround(stepMs / cycle)), 1, kMaxCycles);

    for (int part = 0; part < parts; part++) {
        int steps = units / parts + (part < units % parts ? 1 : 0);
        program.push_back(ramp(slow, cycles, down, steps));
    }
}

}  // namespace

std::string PulseProgram::forChannel(int channel, uint8_t together, uint8_t brightness,
                                     uint32_t periodMs) {
    std::vector<uint16_t> program;
    uint8_t others = together & ~(1u << channel);

    if (others) {
        program.push_back(sendTo(others));
        program.push_back(waitFor(others));
    }

    double rampMs = std::max(0.0, (static_cast<double>(periodMs) - 2 * kPauseMs) / 2);

    program.push_back(setPwm(0));
    appendRamp(program, brightness, rampMs, false);
    program.push_back(pause());
    appendRamp(program, brightness, rampMs, true);
    program.push_back(pause());

    std::string text;
    char word[5];

    for (uint16_t instruction : program) {
        snprintf(word, sizeof(word), "%04x", instruction);
        text += word;
    }
    return text;
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
