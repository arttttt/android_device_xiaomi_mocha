/*
 * Copyright (C) 2018 The LineageOS Project
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

#include "Light.h"

#include <vector>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

static const std::string LCD_BRIGHTNESS =
        "/sys/class/backlight/lcd-backlight/brightness";
static const std::string LCD_MAX_BRIGHTNESS =
        "/sys/class/backlight/lcd-backlight/max_brightness";
static const std::string BUTTONS_BRIGHTNESS =
        "/sys/class/leds/button-backlight/brightness";
static const std::string BUTTONS_MAX_BRIGHTNESS =
        "/sys/class/leds/button-backlight/max_brightness";

/* A colour asked of a lamp that has one brightness and no colour.
 *
 * The weights are the usual approximation of how much each primary
 * contributes to how bright a colour looks, which is not how much each
 * contributes to it numerically -- green carries most of the impression of
 * brightness and blue almost none. */
static uint32_t brightnessOf(const LightState& state) {
    uint32_t colour = state.color & 0x00ffffff;

    return ((77 * ((colour >> 16) & 0xff)) +
            (150 * ((colour >> 8) & 0xff)) +
            (29 * (colour & 0xff))) >> 8;
}

static bool isLit(const LightState& state) {
    return state.color & 0x00ffffff;
}

Light::Light()
    : mLcd(LCD_BRIGHTNESS, LCD_MAX_BRIGHTNESS),
      mButtons(BUTTONS_BRIGHTNESS, BUTTONS_MAX_BRIGHTNESS) {}

Return<Status> Light::setLight(Type type, const LightState& state) {
    switch (type) {
        case Type::BACKLIGHT:
            mLcd.set(brightnessOf(state));
            return Status::SUCCESS;

        case Type::BUTTONS:
            mButtons.set(brightnessOf(state));
            return Status::SUCCESS;

        case Type::ATTENTION:
        case Type::BATTERY:
        case Type::NOTIFICATIONS: {
            std::lock_guard<std::mutex> lock(mLedLock);

            if (type == Type::ATTENTION) mAttention = state;
            else if (type == Type::BATTERY) mBattery = state;
            else mNotification = state;

            showHighestPriority();
            return Status::SUCCESS;
        }

        default:
            return Status::LIGHT_NOT_SUPPORTED;
    }
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    _hidl_cb(std::vector<Type>{Type::BACKLIGHT, Type::BUTTONS, Type::ATTENTION,
                               Type::BATTERY, Type::NOTIFICATIONS});
    return Void();
}

/* One diode, three things that want it.
 *
 * A notification is what the user is waiting to see, so it comes first.
 * Attention is the system asking for a look and comes next. The battery is
 * the standing condition underneath both, shown when nothing is on top of it.
 */
void Light::showHighestPriority() {
    const LightState* shown = nullptr;

    if (isLit(mNotification)) shown = &mNotification;
    else if (isLit(mAttention)) shown = &mAttention;
    else if (isLit(mBattery)) shown = &mBattery;

    if (shown == nullptr) {
        mLed.off();
        return;
    }

    bool timed = shown->flashMode == Flash::TIMED;

    mLed.show(shown->color, timed ? shown->flashOnMs : 0,
              timed ? shown->flashOffMs : 0);
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
