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

#define LOG_TAG "mochasensors"

#include "mochasensors/Calibration.h"

#include <cstdio>
#include <cstring>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

namespace mocha {
namespace sensors {

namespace {

/* The name the hub's core device goes by. */
constexpr const char* kHub = "lsm6db0";

std::string OffsetBytes(const int16_t offset[3]) {
    /* Little-endian, as the hub's firmware reads it; so is this SoC. */
    return std::string(reinterpret_cast<const char*>(offset), 3 * sizeof(int16_t));
}

}  // namespace

/* ---- AccelCalibration ---- */

bool AccelCalibration::Load(const std::string& path) {
    std::string text;
    if (!android::base::ReadFileToString(path, &text)) return false;

    AccelCalibration loaded;
    bool sawOffset = false, sawScale = false;
    for (const auto& line : android::base::Split(text, "\n")) {
        int o[3];
        float s[3];
        if (sscanf(line.c_str(), "offset %d %d %d", &o[0], &o[1], &o[2]) == 3) {
            for (int i = 0; i < 3; i++) loaded.offset[i] = static_cast<int16_t>(o[i]);
            sawOffset = true;
        } else if (sscanf(line.c_str(), "scale %f %f %f", &s[0], &s[1], &s[2]) == 3) {
            /* Within a tenth of one: anything further off is no calibration
             * of this chip but a mistake. */
            for (int i = 0; i < 3; i++)
                if (s[i] < 0.9f || s[i] > 1.1f) return false;
            for (int i = 0; i < 3; i++) loaded.scale[i] = s[i];
            sawScale = true;
        }
    }
    if (!sawOffset && !sawScale) return false;
    loaded.present = true;
    *this = loaded;
    return true;
}

bool AccelCalibration::Save(const std::string& path) const {
    std::string text = android::base::StringPrintf(
            "offset %d %d %d\nscale %.6f %.6f %.6f\n", offset[0], offset[1], offset[2], scale[0],
            scale[1], scale[2]);
    return android::base::WriteStringToFile(text, path);
}

/* ---- HubCalibration ---- */

bool HubCalibration::Open() {
    return core_.Find(kHub);
}

bool HubCalibration::WriteOffset(const char* attribute, const std::string& bytes,
                                 const char* what) {
    if (core_.WriteAttribute(attribute, bytes)) return true;
    LOG(ERROR) << "cannot give the hub its " << what;
    return false;
}

void HubCalibration::Restore() {
    std::string bytes;
    if (android::base::ReadFileToString(kGyro, &bytes)) WriteOffset("gyro_offset_data", bytes, "gyroscope offset");
    if (android::base::ReadFileToString(kMagn, &bytes)) WriteOffset("magn_offset_data", bytes, "magnetometer state");
    ReloadAccel();
}

/*
 * Only the gyroscope's and the magnetometer's: the hub answers a read of the
 * accelerometer's offset with whatever sample it last had (its firmware
 * fills in no reply for it), and that one is ours anyway.
 */
void HubCalibration::Save() {
    Keep("gyro_offset_data", kGyro);
    Keep("magn_offset_data", kMagn);
}

void HubCalibration::Keep(const char* attribute, const char* path) {
    std::string bytes, kept;
    if (!core_.ReadAttribute(attribute, &bytes) || bytes.empty()) return;
    if (android::base::ReadFileToString(path, &kept) && kept == bytes) return;
    if (!android::base::WriteStringToFile(bytes, path)) PLOG(ERROR) << "cannot keep " << path;
}

bool HubCalibration::ReloadAccel() {
    AccelCalibration loaded;
    if (!loaded.Load(kAccel)) loaded = AccelCalibration();
    accel_ = loaded;
    return WriteOffset("accel_offset_data", OffsetBytes(accel_.offset), "accelerometer offset");
}

bool HubCalibration::ResetMagnetometer() {
    return WriteOffset("force_magn_calibration", "1", "magnetometer reset");
}

}  // namespace sensors
}  // namespace mocha
