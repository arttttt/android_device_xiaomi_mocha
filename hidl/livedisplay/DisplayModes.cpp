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

#define LOG_TAG "vendor.lineage.livedisplay@2.0-service.mocha"

#include "DisplayModes.h"

#include <android-base/properties.h>
#include <log/log.h>

#include <string>

namespace vendor {
namespace lineage {
namespace livedisplay {
namespace V2_0 {
namespace implementation {

namespace {

/* The profile the composer is told about, and the one remembered across a
 * reboot. Both are vendor properties: this service writes them and the
 * composer reads them, and neither is any business of the framework's. */
constexpr const char* kSaturationProp = "persist.vendor.hwc.display_saturation";
constexpr const char* kProfileProp = "persist.vendor.hwc.display_profile";

/*
 * The profiles, and the saturation each one stands for. This table is the
 * whole of what a profile is: the composer is handed the number and builds
 * the matrix from it, so a fourth entry here needs nothing anywhere else.
 *
 * Standard is the panel as calibrated. The other two are the values this
 * device has carried since the profiles were first written for it, back
 * when the same table lived in a Java class inside system_server.
 */
struct Profile {
    int32_t id;
    const char* name;
    const char* saturation;
};

constexpr Profile kProfiles[] = {
        {0, "Standard", "1.0"},
        {1, "Cinema", "1.35"},
        {2, "Dynamic", "1.5"},
};

constexpr size_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

}  // anonymous namespace

DisplayModes::DisplayModes() : mCurrentID(0) {
    /* The remembered profile is applied here rather than waited for: the
     * framework asks this service what the modes are only when someone
     * opens the settings page, and the panel should already look the way
     * it was left long before that. */
    mCurrentID = clampID(::android::base::GetIntProperty(kProfileProp, 0));
    apply(mCurrentID);
}

bool DisplayModes::isSupported() const {
    /* Nothing to probe. The profiles are matrices the display controller
     * applies on every frame anyway; there is no block to be missing. */
    return true;
}

int32_t DisplayModes::clampID(int32_t modeID) {
    return (modeID < 0 || static_cast<size_t>(modeID) >= kProfileCount) ? 0 : modeID;
}

DisplayMode DisplayModes::modeByID(int32_t modeID) {
    const Profile& profile = kProfiles[clampID(modeID)];
    return DisplayMode{profile.id, profile.name};
}

bool DisplayModes::apply(int32_t modeID) {
    const Profile& profile = kProfiles[clampID(modeID)];
    if (!::android::base::SetProperty(kSaturationProp, profile.saturation)) {
        ALOGE("could not set %s to %s", kSaturationProp, profile.saturation);
        return false;
    }
    ALOGI("profile %s (saturation %s)", profile.name, profile.saturation);
    return true;
}

Return<void> DisplayModes::getDisplayModes(getDisplayModes_cb _hidl_cb) {
    std::vector<DisplayMode> modes;
    modes.reserve(kProfileCount);
    for (const Profile& profile : kProfiles) {
        modes.push_back(DisplayMode{profile.id, profile.name});
    }
    _hidl_cb(modes);
    return Void();
}

Return<void> DisplayModes::getCurrentDisplayMode(getCurrentDisplayMode_cb _hidl_cb) {
    _hidl_cb(modeByID(mCurrentID));
    return Void();
}

Return<void> DisplayModes::getDefaultDisplayMode(getDefaultDisplayMode_cb _hidl_cb) {
    _hidl_cb(modeByID(::android::base::GetIntProperty(kProfileProp, 0)));
    return Void();
}

Return<bool> DisplayModes::setDisplayMode(int32_t modeID, bool makeDefault) {
    if (modeID != clampID(modeID)) {
        ALOGE("no profile %d", modeID);
        return false;
    }

    if (!apply(modeID)) {
        return false;
    }
    mCurrentID = modeID;

    /* Remembered only when asked to be. The saturation property above is
     * persistent either way, so a profile chosen for this session alone is
     * still what the composer sees at the next boot before this service is
     * up -- which is the panel not flickering through neutral on the way to
     * where it was left. This one decides what it settles on after. */
    if (makeDefault && !::android::base::SetProperty(kProfileProp, std::to_string(modeID))) {
        ALOGE("could not remember profile %d", modeID);
        return false;
    }

    return true;
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace livedisplay
}  // namespace lineage
}  // namespace vendor
