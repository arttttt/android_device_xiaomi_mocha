/*
 * Copyright (C) 2018 The LineageOS Project
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

#include <fstream>
#include <mutex>
#include <unordered_map>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

struct Light : public ILight {
    Light(std::pair<std::ofstream, uint32_t>&& lcd_backlight, std::pair<std::ofstream, uint32_t>&& button_backlight,
          std::ofstream&& red_led, std::ofstream&& green_led, std::ofstream&& blue_led, std::ofstream&& led_select_engine, 
          std::ofstream&& led_run_engine);

    // Methods from ::android::hardware::light::V2_0::ILight follow.
    Return<Status> setLight(Type type, const LightState& state) override;
    Return<void> getSupportedTypes(getSupportedTypes_cb _hidl_cb) override;

  private:
    void setAttentionLight(const LightState& state);
    void setBatteryLight(const LightState& state);
    void setButtonsBacklight(const LightState& state);
    void setLcdBacklight(const LightState& state);
    void setNotificationLight(const LightState& state);
    void setSpeakerBatteryLightLocked();
    void setSpeakerLightLocked(const LightState& state);

    std::pair<std::ofstream, uint32_t> mLcdBacklight;
    std::pair<std::ofstream, uint32_t> mButtonBacklight;
    std::ofstream mRedLed;
    std::ofstream mGreenLed;
    std::ofstream mBlueLed;
    std::ofstream mLedSelectEngine;
    std::ofstream mLedRunEngine;

    LightState mAttentionState;
    LightState mBatteryState;
    LightState mNotificationState;

    std::unordered_map<Type, std::function<void(const LightState&)>> mLights;
    std::mutex mLock;
    std::mutex mBacklightLock;

    /* What the LED chip is already showing. The framework re-asserts the
     * battery light on every clock tick; without this memory each assert
     * turned the diode off and back on. */
    bool mSpeakerSet = false;
    uint32_t mSpeakerColor = 0;
    int mSpeakerOnMs = 0;
    int mSpeakerOffMs = 0;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_LIGHT_V2_0_LIGHT_H
