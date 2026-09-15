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

#define LOG_TAG "android.hardware.power@1.3-service.mocha"

#include <SysfsNode.h>
#include <hardware/power.h>
#include <log/log.h>

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
static const std::string IO_IS_BUSY_NODE =
        "/sys/devices/system/cpu/cpufreq/interactive/io_is_busy";

/* Not declared by power.h on P; the LineageOS extension that used to provide
 * it is gone, and this still dispatches on it. */
const static power_hint_t POWER_HINT_SET_PROFILE = (power_hint_t) 0x00000111;

Power::Power() {
    ALOGI("power_init\n");
}

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
    if (!interactive) mGpuFloor.release();

    return Void();
}

Return<void> Power::powerHint(PowerHint hint, int32_t data) {
    handleHint(hint, data);
    return Void();
}

void Power::handleHint(PowerHint hint, int32_t data) {
    if (static_cast<power_hint_t>(hint) == POWER_HINT_SET_PROFILE) {
        ALOGI("set power profile = %d", data);
        mProfiles.set(data);
        return;
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
             * the one the user chose wins. */
            if (!mProfiles.isPowerSave()) mBoost.pulse();
            break;

        case PowerHint::LOW_POWER:
            handleLowPower(data != 0);
            break;

        default:
            /* VSYNC arrives constantly and says only that something is
             * watching for blanks; the rest do not apply to this board.
             * Deliberately nothing. */
            break;
    }
}

/*
 * The battery saver going on and off.
 *
 * It is the same thing the profile tile already expresses, so it is expressed
 * that way -- one notion of how hard this device should try, not two that can
 * disagree.
 */
void Power::handleLowPower(bool on) {
    std::lock_guard<std::mutex> lock(mPolicyLock);

    if (!on) {
        mProfiles.leaveLowPower();
        return;
    }

    mProfiles.enterLowPower();

    /* And let go of anything already being spent. A floor raised a moment
     * before the battery saver came on is exactly what the battery saver is
     * refusing, and it would otherwise stay up until the composition that
     * asked for it ended. */
    mGpuFloor.release();
}

/*
 * SurfaceFlinger saying the composition has fallen to the GPU and will stay
 * there -- a rotation, a screenshot, a layer the composer would not take.
 */
void Power::handleExpensiveRendering(bool expensive) {
    std::lock_guard<std::mutex> lock(mPolicyLock);

    if (!expensive) {
        mGpuFloor.release();
        return;
    }

    /* Held back under the battery saver for the same reason a touch does not
     * get a boost pulse: the user asked this device to spend less, and
     * spending the GPU on frames is spending. */
    if (mProfiles.isPowerSave()) return;

    mGpuFloor.hold();
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
        return Profiles::COUNT;
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
        handleExpensiveRendering(data != 0);
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
