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
#ifndef ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H
#define ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H

#include <android/hardware/light/2.0/ILight.h>
#include <hidl/Status.h>

#include <mutex>

#include "Backlight.h"
#include "NotificationLed.h"

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;

/*
 * Which lamp answers which kind of light, and -- for the one diode three
 * kinds share -- which of them wins.
 *
 * The lamps themselves are elsewhere: this decides, they act.
 */
struct Light : public ILight {
    Light();

    Return<Status> setLight(Type type, const LightState& state) override;
    Return<void> getSupportedTypes(getSupportedTypes_cb _hidl_cb) override;

  private:
    void showHighestPriority();

    Backlight mLcd;
    Backlight mButtons;
    NotificationLed mLed;

    /* The three kinds of light that share the diode, each remembered whether
     * or not it is the one currently shown. */
    LightState mAttention;
    LightState mBattery;
    LightState mNotification;

    /* Guards the three states above and the diode, which they decide
     * together. The backlights share nothing with them and are not held up
     * behind them. */
    std::mutex mLedLock;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H
