/*
 * Copyright (C) 2026 The LineageOS Project
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

#include "NotificationLed.h"

#include <SysfsNode.h>

#include <string>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

static const std::string RED = "/sys/class/leds/red/brightness";
static const std::string GREEN = "/sys/class/leds/green/brightness";
static const std::string BLUE = "/sys/class/leds/blue/brightness";

/* The chip's pattern engine: which channels it drives, and whether it runs. */
static const std::string SELECT_ENGINE =
        "/sys/bus/i2c/drivers/lp5521/0-0032/select_engine";
static const std::string RUN_ENGINE =
        "/sys/bus/i2c/drivers/lp5521/0-0032/run_engine";

static Rgb unpack(uint32_t colour) {
    return {static_cast<uint8_t>((colour >> 16) & 0xff),
            static_cast<uint8_t>((colour >> 8) & 0xff),
            static_cast<uint8_t>(colour & 0xff)};
}

void NotificationLed::show(uint32_t colour, uint32_t onMs, uint32_t offMs) {
    colour &= 0x00ffffff;

    if (alreadyShowing(colour, onMs, offMs)) return;

    clear();

    Rgb wanted = unpack(colour);
    bool blinking = onMs > 0 && offMs > 0;

    if (blinking) {
        showBlinking(Palette::nearest(wanted));
    } else {
        showSteady(wanted);
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

/* Stop the engine and put every channel out.
 *
 * Nothing waits between these writes: the driver orders them itself and sits
 * out the chip's settling times, which are microseconds, inside its worker. */
void NotificationLed::clear() const {
    mocha::sysfs::write(RUN_ENGINE, 0);
    mocha::sysfs::write(RED, 0);
    mocha::sysfs::write(GREEN, 0);
    mocha::sysfs::write(BLUE, 0);
}

void NotificationLed::showSteady(Rgb colour) const {
    mocha::sysfs::write(RED, colour.r);
    mocha::sysfs::write(GREEN, colour.g);
    mocha::sysfs::write(BLUE, colour.b);
}

/* Hand the colour to the engine, which then blinks it without us.
 *
 * A channel is named to the engine if the colour uses it at all; the engine
 * drives the named ones together, which is why the colour has to be one it
 * can hold -- see Palette.
 *
 * The rhythm asked for is not passed on, because there is nowhere to pass it:
 * the engine runs a pattern the driver programmed into the chip, and what we
 * choose is whether it runs, not how fast. A request to blink twice a second
 * and one to blink every five both come out the same. The old code clamped
 * the two durations to between one and seven seconds before remembering them,
 * which looked like it was doing something -- but it never wrote them
 * anywhere either, and since it compared the unclamped values against the
 * clamped ones, any request outside that range was treated as new every time
 * and reprogrammed the diode for nothing. */
void NotificationLed::showBlinking(Rgb colour) const {
    if (colour.r) mocha::sysfs::write(SELECT_ENGINE, 1);
    if (colour.g) mocha::sysfs::write(SELECT_ENGINE, 2);
    if (colour.b) mocha::sysfs::write(SELECT_ENGINE, 3);

    mocha::sysfs::write(RUN_ENGINE, 1);
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
