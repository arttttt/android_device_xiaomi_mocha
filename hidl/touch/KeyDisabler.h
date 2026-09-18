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

#ifndef MOCHA_TOUCH_KEYDISABLER_H
#define MOCHA_TOUCH_KEYDISABLER_H

#include <vendor/lineage/touch/1.0/IKeyDisabler.h>

namespace vendor {
namespace lineage {
namespace touch {
namespace V1_0 {
namespace implementation {

using ::android::hardware::Return;

/*
 * Turns the capacitive keys off, for whoever prefers the navigation bar.
 *
 * The node this board offers says whether the keys are ON; the interface
 * asks whether the disabler is. One is the other's opposite, and keeping
 * that in one place here is the whole of the translation -- the class this
 * replaces read the node without inverting it, and so reported the disabler
 * as engaged exactly when the keys were working.
 */
class KeyDisabler : public IKeyDisabler {
  public:
    KeyDisabler();

    bool isSupported() const;

    // Methods from ::vendor::lineage::touch::V1_0::IKeyDisabler follow.
    Return<bool> isEnabled() override;
    Return<bool> setEnabled(bool enabled) override;

  private:
    bool mSupported;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace touch
}  // namespace lineage
}  // namespace vendor

#endif  // MOCHA_TOUCH_KEYDISABLER_H
