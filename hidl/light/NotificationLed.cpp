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

#define LOG_TAG "android.hardware.light@2.0-service.mocha"

#include "NotificationLed.h"

#include <SysfsNode.h>

#include <string>

#include "PulseProgram.h"

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

namespace {

const std::string CHIP = "/sys/bus/i2c/drivers/lp5521/0-0032/";

/* One of the diode's three colours: its brightness under direct control,
 * and the engine that runs it otherwise. */
struct Channel {
    std::string brightness;
    std::string engineMode;
    std::string engineLoad;
};

const std::array<Channel, 3> CHANNELS = {{
    {"/sys/class/leds/red/brightness", CHIP + "engine1_mode", CHIP + "engine1_load"},
    {"/sys/class/leds/green/brightness", CHIP + "engine2_mode", CHIP + "engine2_load"},
    {"/sys/class/leds/blue/brightness", CHIP + "engine3_mode", CHIP + "engine3_load"},
}};

/* 1 starts every engine holding a fresh program, all in one write; 0 hands
 * the channels back to direct control. */
const std::string RUN_ENGINE = CHIP + "run_engine";

const std::string DISABLED = "disabled";

}  // namespace

void NotificationLed::show(uint32_t colour, uint32_t onMs, uint32_t offMs) {
    colour &= 0x00ffffff;

    if (alreadyShowing(colour, onMs, offMs)) return;

    clear();

    Levels levels = {static_cast<uint8_t>((colour >> 16) & 0xff),
                     static_cast<uint8_t>((colour >> 8) & 0xff),
                     static_cast<uint8_t>(colour & 0xff)};
    bool flashing = onMs > 0 && offMs > 0;

    if (flashing) {
        showPulsing(levels, onMs + offMs);
    } else {
        showSteady(levels);
    }

    remember(colour, onMs, offMs);
}

void NotificationLed::off() {
    if (alreadyShowing(0, 0, 0)) return;

    clear();
    remember(0, 0, 0);
}

bool NotificationLed::alreadyShowing(uint32_t colour, uint32_t onMs, uint32_t offMs) const {
    return mSet && colour == mColour && onMs == mOnMs && offMs == mOffMs;
}

void NotificationLed::remember(uint32_t colour, uint32_t onMs, uint32_t offMs) {
    mSet = true;
    mColour = colour;
    mOnMs = onMs;
    mOffMs = offMs;
}

/* Stop the engines and put every channel out.
 *
 * A running engine may not go straight back to direct control (the
 * datasheet's RUN Program section), and run_engine 0 stops only the last
 * engine the driver was given -- so each is disabled first, which also
 * rewinds its program, and then all three are handed back. Nothing waits
 * between these writes: the driver orders them itself and sits out the
 * chip's settling times, which are microseconds, inside its worker. */
void NotificationLed::clear() const {
    for (const Channel& channel : CHANNELS) mocha::sysfs::write(channel.engineMode, DISABLED);

    mocha::sysfs::write(RUN_ENGINE, 0);

    for (const Channel& channel : CHANNELS) mocha::sysfs::write(channel.brightness, 0);
}

void NotificationLed::showSteady(const Levels& levels) const {
    for (size_t i = 0; i < CHANNELS.size(); i++) {
        mocha::sysfs::write(CHANNELS[i].brightness, levels[i]);
    }
}

/* Give every channel the colour uses a program of its own, then start them
 * together.
 *
 * engineN_load is synchronous: the driver puts the engine into its load
 * mode and writes the program before the write returns, so run_engine
 * finds all of them ready. select_engine, which this used to go through,
 * read the program from /vendor/firmware/lp5521 on a worker of its own and
 * had the one breath for every request: no brighter than half scale, which
 * the driver's logarithmic dimming makes barely visible, the same speed
 * whatever was asked, and only in the colours a single program could hold
 * on all three channels at once. */
void NotificationLed::showPulsing(const Levels& levels, uint32_t periodMs) const {
    uint8_t together = 0;

    for (size_t i = 0; i < CHANNELS.size(); i++) {
        if (levels[i]) together |= 1u << i;
    }

    for (size_t i = 0; i < CHANNELS.size(); i++) {
        if (levels[i] == 0) continue;

        mocha::sysfs::write(CHANNELS[i].engineLoad,
                            PulseProgram::forChannel(i, together, levels[i], periodMs));
    }

    mocha::sysfs::write(RUN_ENGINE, 1);
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
