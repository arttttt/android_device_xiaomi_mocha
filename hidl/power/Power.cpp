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

#define LOG_TAG "android.hardware.power-service.mocha"

#include "Power.h"

#include <SysfsNode.h>

#include <log/log.h>

namespace aidl::android::hardware::power::impl::mocha {

static const std::string TAP_TO_WAKE_NODE = "/proc/touchpanel/double_tap_enable";
static const std::string IO_IS_BUSY_NODE =
        "/sys/devices/system/cpu/cpufreq/interactive/io_is_busy";

Power::Power() {
    ALOGI("power_init\n");
}

ndk::ScopedAStatus Power::setMode(Mode type, bool enabled) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
            ::mocha::sysfs::write(TAP_TO_WAKE_NODE, enabled ? 1 : 0);
            break;

        case Mode::LOW_POWER:
            handleLowPower(enabled);
            break;

        case Mode::EXPENSIVE_RENDERING:
            handleExpensiveRendering(enabled);
            break;

        case Mode::INTERACTIVE:
            setInteractive(enabled);
            break;

        case Mode::LAUNCH:
            /* An application being opened. It begins and ends; the pulse
             * answers the beginning, and the governor drops the clock again
             * by itself. */
            if (enabled) hurry();
            break;

        case Mode::CAMERA_STREAMING_LOW:
            streamCamera(CameraHints::LOW, enabled);
            break;
        case Mode::CAMERA_STREAMING_MID:
            streamCamera(CameraHints::MID, enabled);
            break;
        case Mode::CAMERA_STREAMING_HIGH:
            streamCamera(CameraHints::HIGH, enabled);
            break;

        default:
            /* Not answered here; isModeSupported says so. */
            break;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isModeSupported(Mode type, bool* _aidl_return) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
        case Mode::LOW_POWER:
        case Mode::EXPENSIVE_RENDERING:
        case Mode::INTERACTIVE:
        case Mode::LAUNCH:
        case Mode::CAMERA_STREAMING_LOW:
        case Mode::CAMERA_STREAMING_MID:
        case Mode::CAMERA_STREAMING_HIGH:
            *_aidl_return = true;
            break;
        default:
            *_aidl_return = false;
            break;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::setBoost(Boost type, int32_t durationMs) {
    switch (type) {
        case Boost::INTERACTION:
            /* A finger on the glass. How long a touch is worth is the
             * performance profile's boostpulse_duration, not the caller's. */
            hurry();
            break;

        case Boost::CAMERA_LAUNCH:
        case Boost::CAMERA_SHOT: {
            std::lock_guard<std::mutex> lock(mPolicyLock);
            mCamera.setSuppressed(sparingLocked());
            if (type == Boost::CAMERA_LAUNCH)
                mCamera.launch();
            else
                mCamera.shot(durationMs);
            break;
        }

        default:
            break;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isBoostSupported(Boost type, bool* _aidl_return) {
    *_aidl_return = type == Boost::INTERACTION || type == Boost::CAMERA_LAUNCH ||
                    type == Boost::CAMERA_SHOT;
    return ndk::ScopedAStatus::ok();
}

/* Hint sessions want a governor that takes per-thread work durations; the
 * interactive governor takes none, so there is nothing to give them to. The
 * framework hears "unsupported" and does not ask again. */
ndk::ScopedAStatus Power::createHintSession(int32_t, int32_t, const std::vector<int32_t>&, int64_t,
                                            std::shared_ptr<IPowerHintSession>* _aidl_return) {
    *_aidl_return = nullptr;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::getHintSessionPreferredRate(int64_t* outNanoseconds) {
    *outNanoseconds = -1;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

void Power::setInteractive(bool interactive) {
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
    ::mocha::sysfs::write(IO_IS_BUSY_NODE, interactive ? 1 : 0);

    /* Nothing is being composed for anyone with the screen off, so any floor
     * held for composition is held for no one. SurfaceFlinger does say so
     * itself, but it says it in its own time, and there is no reason to spend
     * the difference. */
    if (!interactive) mGpuFloor.release();
}

/*
 * Someone is waiting on this: a finger on the glass, or an application being
 * opened. The same answer serves both -- pulse the clock up and let the
 * governor drop it again.
 *
 * Not while the battery saver is on. Being asked to hurry and being told to
 * save power are contradictory instructions, and the one the user chose wins.
 */
void Power::hurry() {
    if (!mProfiles.isPowerSave()) mBoost.pulse();
}

/* The profile can be changed from the tile with no hint to say so, so whether
 * the camera may be helped is asked again with every one of its hints. */
void Power::streamCamera(CameraHints::Level level, bool on) {
    std::lock_guard<std::mutex> lock(mPolicyLock);
    mCamera.setSuppressed(sparingLocked());
    mCamera.stream(level, on);
}

bool Power::sparingLocked() const {
    return mLowPower || mProfiles.chosen() == Profiles::POWER_SAVE;
}

/*
 * The battery saver going on and off.
 *
 * It is the same thing the profile tile already expresses, so it is expressed
 * that way -- one notion of how hard this device should try, not two that can
 * disagree. power.mocha.rc holds the profile at power save while it lasts and
 * puts the user's choice back after.
 */
void Power::handleLowPower(bool on) {
    std::lock_guard<std::mutex> lock(mPolicyLock);

    mLowPower = on;
    mProfiles.setLowPower(on);

    /* The camera's floors go with the saver and come back after it, if
     * the camera is still streaming. */
    mCamera.setSuppressed(sparingLocked());
    if (!on) return;

    /* And let go of anything already being spent. A floor raised a moment
     * before the battery saver came on is exactly what the battery saver is
     * refusing, and it would otherwise stay up until the composition that
     * asked for it ended. */
    mGpuFloor.release();
}

/*
 * SurfaceFlinger saying the composition has fallen to the GPU and will stay
 * there -- a rotation, a screenshot, a layer the composer would not take.
 * It is rare on this board, where every layer of an ordinary scene is
 * composed by the hardware, which is exactly why the GPU is at its floor when
 * it does happen.
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

}  // namespace aidl::android::hardware::power::impl::mocha
