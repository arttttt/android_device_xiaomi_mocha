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

#ifndef MOCHA_LIVEDISPLAY_DISPLAYMODES_H
#define MOCHA_LIVEDISPLAY_DISPLAYMODES_H

#include <vendor/lineage/livedisplay/2.0/IDisplayModes.h>

#include <cstdint>
#include <vector>

namespace vendor {
namespace lineage {
namespace livedisplay {
namespace V2_0 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;

/*
 * Display profiles for a panel that has none of its own.
 *
 * The classic implementations of this interface write a mode number into a
 * panel's sysfs, or hand it to a vendor library that does. This panel has
 * neither: what it has is the display controller's colour matrix, which the
 * composer feeds into the CMU on every frame.
 *
 * So a profile here is a saturation, and this service's whole job is to say
 * which one. It writes the number into a property; the composer notices,
 * builds the matrix and asks for a frame. Nothing of the matrix lives here
 * and nothing of the profile list lives there -- adding a profile is a line
 * in the table below and no change at all on the other side.
 *
 * It ran as a Java class inside system_server until Android 10, where the
 * overlay mechanism that carried such classes was removed. A vendor process
 * cannot reach the framework's transform manager the way that class did,
 * which is why the composer applies it now and this only chooses.
 */
class DisplayModes : public IDisplayModes {
  public:
    DisplayModes();

    bool isSupported() const;

    // Methods from ::vendor::lineage::livedisplay::V2_0::IDisplayModes follow.
    Return<void> getDisplayModes(getDisplayModes_cb _hidl_cb) override;
    Return<void> getCurrentDisplayMode(getCurrentDisplayMode_cb _hidl_cb) override;
    Return<void> getDefaultDisplayMode(getDefaultDisplayMode_cb _hidl_cb) override;
    Return<bool> setDisplayMode(int32_t modeID, bool makeDefault) override;

  private:
    static bool apply(int32_t modeID);
    static int32_t clampID(int32_t modeID);
    static DisplayMode modeByID(int32_t modeID);

    int32_t mCurrentID;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace livedisplay
}  // namespace lineage
}  // namespace vendor

#endif  // MOCHA_LIVEDISPLAY_DISPLAYMODES_H
