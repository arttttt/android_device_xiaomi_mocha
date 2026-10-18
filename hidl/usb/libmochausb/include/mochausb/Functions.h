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
#ifndef MOCHA_USB_FUNCTIONS_H
#define MOCHA_USB_FUNCTIONS_H

#include <cstdint>

namespace mocha {
namespace usb {

/*
 * The functions a gadget can carry, numbered as the USB gadget HALs number
 * them. HIDL and AIDL use the same bits; each service checks that it still
 * does, so this library needs neither.
 */
namespace function {
constexpr uint64_t kNone = 0;
constexpr uint64_t kAdb = 1 << 0;
constexpr uint64_t kAccessory = 1 << 1;
constexpr uint64_t kMtp = 1 << 2;
constexpr uint64_t kMidi = 1 << 3;
constexpr uint64_t kPtp = 1 << 4;
constexpr uint64_t kRndis = 1 << 5;
constexpr uint64_t kAudioSource = 1 << 6;
constexpr uint64_t kUvc = 1 << 7;
constexpr uint64_t kNcm = 1 << 10;

/* What this board has an instance of. MIDI, UVC and NCM it does not. */
constexpr uint64_t kSupported = kAdb | kAccessory | kMtp | kPtp | kRndis | kAudioSource;
}  // namespace function

}  // namespace usb
}  // namespace mocha

#endif  // MOCHA_USB_FUNCTIONS_H
