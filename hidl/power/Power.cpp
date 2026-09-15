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

#define LOG_TAG "android.hardware.power@1.3-service.mocha"

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
#include <SysfsNode.h>

#include "Power.h"

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

using ::android::hardware::power::V1_0::Feature;
using ::android::hardware::power::V1_0::PowerHint;
using ::android::hardware::power::V1_0::PowerStatePlatformSleepState;
using ::android::hardware::power::V1_0::Status;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

static const std::string TAP_TO_WAKE_NODE = "/proc/touchpanel/double_tap_enable";
static const std::string GPU_FLOOR_NODE = "/sys/kernel/tegra_gpu/gpu_floor_rate";

/* The floor the GPU is allowed to fall to, and the one to hold while the
 * composition is expensive.
 *
 * 72 MHz is the bottom of gpu_available_rates and where the clock sits
 * whenever nothing is asking for the GPU -- measured here, 57 samples out of
 * 60 while an application was being launched. The same measurement is where
 * 252 MHz comes from: it is the step the governor reaches by itself when
 * something does happen. So holding it as a floor does not overrule the
 * governor's own judgement of what this work costs; it stops the clock
 * dropping back to the bottom between frames, which is the only part the
 * governor gets wrong here.
 *
 * It is a choice, not a measurement of the scene it is for. Forcing client
 * composition to measure that properly needs SurfaceFlinger's debug
 * transaction, which answers only to uid system, and root is not it. If a
 * scene that does fall to the GPU ever turns up, sample gpu_rate during it
 * and let the number settle the argument.
 */
static const std::string GPU_FLOOR_IDLE = "72000000";
static const std::string GPU_FLOOR_RENDER = "252000000";
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

/* Everything this service remembers about what it has done to the machine,
 * and the lock over it.
 *
 * Both are read and written from hint handlers, and hints arrive from more
 * than one caller: the framework sends INTERACTION and LOW_POWER,
 * SurfaceFlinger sends EXPENSIVE_RENDERING, and from 1.1 they arrive one-way,
 * which is to say the callers do not take turns. Whether two of them can be
 * in here at once is a property of the thread pool, and the thread pool is one
 * number in service.cpp -- not a thing to build correctness on.
 */
static std::mutex state_lock;

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

        /* And once when it opens, if it had not before. Without this the log
         * is silent in both cases, so silence says nothing about which of the
         * two is happening -- which is how this node stayed unwritable for
         * months. init grants it after the governor is chosen, so the first
         * hints of a boot can legitimately arrive before it exists. */
        if (boostpulse_complained) {
            boostpulse_complained = false;
            ALOGI("%s opened; touch and launch hints raise the clock",
                  BOOSTPULSE_NODE.c_str());
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

/* Whether the floor is currently held up, so that letting go is idempotent
 * and a stray "no longer expensive" cannot lower a floor nobody raised.
 * Guarded by state_lock. */
static bool render_floor_held = false;

/* Callers already holding state_lock. */
static void DropRenderFloorLocked() {
    if (!render_floor_held) return;

    mocha::sysfs::write(GPU_FLOOR_NODE, GPU_FLOOR_IDLE);
    render_floor_held = false;
}

static void HoldRenderFloor(bool hold) {
    const std::lock_guard<std::mutex> lock(state_lock);

    if (!hold) {
        DropRenderFloorLocked();
        return;
    }

    /* Not while the battery saver is on, for the same reason a touch does not
     * get a boost pulse under it: the user asked this device to spend less,
     * and spending the GPU on frames is spending. */
    if (CurrentProfile() == PROFILE_POWER_SAVE) return;

    mocha::sysfs::write(GPU_FLOOR_NODE, GPU_FLOOR_RENDER);
    render_floor_held = true;
}

Power::Power() {
    ALOGI("power_init\n");

    /* Whatever happened to the last incarnation of this service, it does not
     * get to leave the GPU floor raised behind it. The node outlives the
     * process, and a floor left at 252 MHz is battery spent on nothing, for
     * as long as the board stays up, with nothing in any log to say why.
     *
     * Doing it here rather than in a destructor is deliberate: a destructor
     * answers only the orderly exits, and those are not the ones that worry
     * me. This answers all of them, because whatever the last one did, the
     * next start puts the floor back.
     */
    mocha::sysfs::write(GPU_FLOOR_NODE, GPU_FLOOR_IDLE);
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
    mocha::sysfs::write(IO_IS_BUSY_NODE, interactive ? 1 : 0);

    /* Nothing is being composed for anyone with the screen off, so any floor
     * held for composition is held for no one. SurfaceFlinger does say so
     * itself, but it says it in its own time, and there is no reason to spend
     * the difference. */
    if (!interactive) HoldRenderFloor(false);

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
            {
                const std::lock_guard<std::mutex> lock(state_lock);

                if (data != 0) {
                    if (profile_before_low_power < 0) {
                        profile_before_low_power = CurrentProfile();
                    }
                    SetProfile(PROFILE_POWER_SAVE);

                    /* And let go of anything already being spent. A floor
                     * raised a moment before the battery saver came on is
                     * exactly what the battery saver is refusing, and it
                     * would otherwise stay up until the composition that
                     * asked for it ended. */
                    DropRenderFloorLocked();
                } else if (profile_before_low_power >= 0) {
                    SetProfile(profile_before_low_power);
                    profile_before_low_power = -1;
                }
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
        mocha::sysfs::write(TAP_TO_WAKE_NODE, activate ? 1 : 0);
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

/* Methods from ::android::hardware::power::V1_1::IPower follow.
 *
 * The same hints as 1.0, asked for without waiting. That is the whole of 1.1,
 * and the whole of why this service is not still 1.0: the framework sends
 * INTERACTION on every touch, and at 1.0 it sends it synchronously -- a
 * system_server thread waiting on a binder round trip, and inside it on a
 * write to sysfs, for every finger that lands on the glass.
 */
Return<void> Power::powerHintAsync(PowerHint hint, int32_t data) {
    return powerHint(hint, data);
}

/* What each subsystem spent asleep, which is asked for by the battery stats
 * service and by nothing else.
 *
 * Answered empty, as the platform states above are. Both want counters the
 * firmware keeps -- how long a modem or a sensor hub or a WLAN block stayed
 * in its own low power state, and how often it got there -- and this board
 * has nothing that keeps them: no modem at all, and nothing else here exposes
 * a residency counter to the kernel, let alone to us.
 *
 * Answering empty with SUCCESS rather than failing is the distinction the
 * interface draws: the question was understood and there is nothing to
 * report, as against the service being broken. The caller then shows no
 * subsystem breakdown, which is the truth. At 1.0 it could not ask at all and
 * logged that this device does not support it.
 */
Return<void> Power::getSubsystemLowPowerStats(getSubsystemLowPowerStats_cb _hidl_cb) {
    hidl_vec<V1_1::PowerStateSubsystem> subsystems;

    subsystems.resize(0);
    _hidl_cb(subsystems, Status::SUCCESS);

    return Void();
}

/* Methods from ::android::hardware::power::V1_2::IPower follow.
 *
 * 1.2 widened the enumeration by five: two for audio, three for the camera.
 * Nothing in the platform sends any of them -- not frameworks/base, not av,
 * not native -- and the vendor stacks on this board do not either, so they
 * arrive here never. Passed through unchanged; the switch they land in
 * ignores what it does not know.
 */
Return<void> Power::powerHintAsync_1_2(V1_2::PowerHint hint, int32_t data) {
    return powerHint(static_cast<PowerHint>(hint), data);
}

/* Methods from ::android::hardware::power::V1_3::IPower follow.
 *
 * 1.3 added one value, and unlike 1.2's five it has a sender: SurfaceFlinger,
 * when the composition has fallen to the GPU and will stay there for a while
 * -- a rotation, a screenshot, a layer the composer would not take. It is
 * rare on this board, where every layer of an ordinary scene is composed by
 * the hardware, which is exactly why the GPU is at its floor when it does
 * happen.
 */
Return<void> Power::powerHintAsync_1_3(V1_3::PowerHint hint, int32_t data) {
    if (hint == V1_3::PowerHint::EXPENSIVE_RENDERING) {
        HoldRenderFloor(data != 0);
        return Void();
    }

    return powerHint(static_cast<PowerHint>(hint), data);
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
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android
