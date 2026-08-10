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

#define LOG_TAG "android.hardware.power@1.0-service.mocha"

#include <android/log.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <utils/Log.h>
#include "Power.h"
#include "sysfs.h"

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace implementation {

using ::android::hardware::power::V1_0::Feature;
using ::android::hardware::power::V1_0::PowerHint;
using ::android::hardware::power::V1_0::PowerStatePlatformSleepState;
using ::android::hardware::power::V1_0::Status;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

static const std::string TAP_TO_WAKE_NODE = "/proc/touchpanel/double_tap_enable";
static const std::string POWER_PROFILE_PROPERTY = "sys.perf.profile";
static const int PROFILE_MAX = 4;

/* Where the framework's hints land.
 *
 * A pulse raises the frequency and lets the governor drop it again after a
 * duration the governor itself was configured with -- so how long a touch is
 * worth is decided by the performance profile, not here. That is the right
 * division: this file knows WHEN, the profile knows HOW MUCH.
 *
 * Writing these fails harmlessly when another governor is in charge, since the
 * files exist only while interactive does. That is the honest failure: the
 * hint is not acted on, rather than acted on wrongly.
 */
static const std::string CPUFREQ_INTERACTIVE = "/sys/devices/system/cpu/cpufreq/interactive/";
static const std::string BOOSTPULSE_NODE = CPUFREQ_INTERACTIVE + "boostpulse";
static const std::string IO_IS_BUSY_NODE = CPUFREQ_INTERACTIVE + "io_is_busy";

/* The profiles, as the quick settings tile numbers them. */
static const int PROFILE_POWER_SAVE = 0;
static const int PROFILE_BALANCED = 1;

/* The descriptor is kept open rather than reopened per hint. Touching produces
 * these continuously, and an open-write-close for each is three system calls
 * spent on saying something that costs one. Every implementation surveyed that
 * takes this hint seriously does the same. */
static std::mutex boost_lock;
static int boostpulse_fd = -1;
static bool boostpulse_complained = false;

/* What the profile was before the battery saver took over, so it can be given
 * back. The framework only says "low power on" and "low power off"; it does
 * not remember what the user had chosen, and neither would we if this were
 * not kept. */
static int profile_before_low_power = -1;

static void SendBoostPulse() {
    const std::lock_guard<std::mutex> lock(boost_lock);

    if (boostpulse_fd < 0) {
        boostpulse_fd = open(BOOSTPULSE_NODE.c_str(), O_WRONLY | O_CLOEXEC);
        if (boostpulse_fd < 0) {
            /* Said once. Under a governor without this node it would otherwise
             * be said on every touch, which is the sort of logging that costs
             * more than what it reports on. */
            if (!boostpulse_complained) {
                boostpulse_complained = true;
                ALOGW("no %s; touch and launch hints will not raise the clock",
                      BOOSTPULSE_NODE.c_str());
            }
            return;
        }
    }

    if (write(boostpulse_fd, "1", 1) < 0) {
        ALOGE("cannot pulse the boost: %s", strerror(errno));
        close(boostpulse_fd);
        boostpulse_fd = -1;
    }
}

static int CurrentProfile() {
    return property_get_int32(POWER_PROFILE_PROPERTY.c_str(), PROFILE_BALANCED);
}

static void SetProfile(int profile) {
    property_set(POWER_PROFILE_PROPERTY.c_str(),
                 std::to_string(profile).c_str());
}

/* Not declared by power.h on P; the LineageOS extension that used to
 * provide it is gone, and Power.cpp still dispatches on it. */
const static power_hint_t POWER_HINT_SET_PROFILE = (power_hint_t) 0x00000111;

Power::Power() {
    ALOGI("power_init\n");
}

// Methods from ::android::hardware::power::V1_0::IPower follow.
Return<void> Power::setInteractive(bool interactive)  {
    /* Whether anything is being shown to anyone.
     *
     * With the screen off, time spent waiting on storage should no longer be
     * counted as the processor being busy: there is no frame waiting on it,
     * and counting it holds the clock up for work nobody is watching. With
     * the screen on it should be counted, since a frame stalled on a read is
     * still a frame the user is waiting for.
     *
     * Deliberately not lowering the frequency ceiling here, although the
     * implementations surveyed do. The ceiling belongs to the performance
     * profile, which the user chooses; picking a second one here would be
     * inventing a number and then quietly overruling them with it.
     */
    utils::sysfs_write(IO_IS_BUSY_NODE, interactive ? "1" : "0");

    return Void();
}

Return<void> Power::powerHint(PowerHint hint, int32_t data) {
    if (static_cast<power_hint_t>(hint) == POWER_HINT_SET_PROFILE) {
        std::string value = std::to_string(data);
        property_set(POWER_PROFILE_PROPERTY.c_str(), value.c_str());
        ALOGI("set power profile = %d", data);
        return Void();
    }

    switch (hint) {
        case PowerHint::INTERACTION:
        case PowerHint::LAUNCH:
            /* Someone is waiting on this: a finger on the glass, or an
             * application being opened. The same answer serves both -- pulse
             * the clock up and let the governor drop it again.
             *
             * Not while the battery saver is on. Being asked to hurry and
             * being told to save power are contradictory instructions, and
             * the one the user chose wins.
             */
            if (CurrentProfile() != PROFILE_POWER_SAVE) {
                SendBoostPulse();
            }
            break;

        case PowerHint::LOW_POWER:
            /* The battery saver going on and off. It is the same thing the
             * profile tile already expresses, so it is expressed that way --
             * one notion of how hard this device should try, not two that can
             * disagree.
             *
             * The profile in force is remembered on the way in and given back
             * on the way out, since the framework does not carry it and would
             * otherwise leave whatever was chosen replaced for good.
             */
            if (data != 0) {
                if (profile_before_low_power < 0) {
                    profile_before_low_power = CurrentProfile();
                }
                SetProfile(PROFILE_POWER_SAVE);
            } else if (profile_before_low_power >= 0) {
                SetProfile(profile_before_low_power);
                profile_before_low_power = -1;
            }
            break;

        default:
            /* VSYNC arrives constantly and says only that something is
             * watching for blanks; the rest do not apply to this board.
             * Deliberately nothing.
             */
            break;
    }

    return Void();
}

Return<void> Power::setFeature(Feature feature, bool activate)  {
    if (feature == Feature::POWER_FEATURE_DOUBLE_TAP_TO_WAKE) {
        ALOGI("POWER_FEATURE_DOUBLE_TAP_TO_WAKE activate = %d\n", activate);
        std::string data = std::to_string(activate);
        utils::sysfs_write(TAP_TO_WAKE_NODE, data);
    }
    return Void();
}

Return<void> Power::getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) {
    hidl_vec<PowerStatePlatformSleepState> states;
    states.resize(0);
    _hidl_cb(states, Status::SUCCESS);
    return Void();
}

Return<int32_t> Power::getFeature(LineageFeature feature)  {
    if (feature == LineageFeature::SUPPORTED_PROFILES) {
        ALOGI("power profiles POWER_FEATURE_SUPPORTED_PROFILES\n");
        return PROFILE_MAX;
    }
    return -1;
}

status_t Power::registerAsSystemService() {
    status_t ret = 0;

    ret = IPower::registerAsService();
    if (ret != 0) {
        ALOGE("Failed to register IPower (%d)", ret);
        goto fail;
    } else {
        ALOGI("Successfully registered IPower");
    }

    ret = ILineagePower::registerAsService();
    if (ret != 0) {
        ALOGE("Failed to register ILineagePower (%d)", ret);
        goto fail;
    } else {
        ALOGI("Successfully registered ILineagePower");
    }

fail:
    return ret;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace power
}  // namespace hardware
}  // namespace android
