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

#include "Palette.h"

#include <array>
#include <cmath>

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

namespace {

struct Lab {
    float l, a, b;
};

/* Every colour the engine can hold while blinking: the primaries and their
 * pairs, plus white, grey and off. */
const std::array<Rgb, 9> kColours = {{
    {255,   0,   0},  /* red */
    {255, 255,   0},  /* yellow */
    {  0, 255,   0},  /* green */
    {  0, 255, 255},  /* cyan */
    {  0,   0, 255},  /* blue */
    {255,   0, 255},  /* magenta */
    {255, 255, 255},  /* white */
    {127, 127, 127},  /* grey */
    {  0,   0,   0},  /* off */
}};

/* sRGB to L*a*b*, by way of XYZ, against D50 white.
 * After Bruce Lindbloom's formulae. */
Lab toLab(Rgb colour) {
    const float eps = 216.f / 24389.f;
    const float k = 24389.f / 27.f;

    /* reference white, D50 */
    const float xr_w = 0.964221f, yr_w = 1.0f, zr_w = 0.825211f;

    auto linear = [](float c) {
        return c <= 0.04045f ? c / 12.f : powf((c + 0.055f) / 1.055f, 2.4f);
    };

    float r = linear(colour.r / 255.f);
    float g = linear(colour.g / 255.f);
    float b = linear(colour.b / 255.f);

    float x = 0.436052025f * r + 0.385081593f * g + 0.143087414f * b;
    float y = 0.222491598f * r + 0.71688606f  * g + 0.060621486f * b;
    float z = 0.013929122f * r + 0.097097002f * g + 0.71418547f  * b;

    auto f = [&](float t) {
        return t > eps ? powf(t, 1.f / 3.f) : (k * t + 16.f) / 116.f;
    };

    float fx = f(x / xr_w), fy = f(y / yr_w), fz = f(z / zr_w);

    return {2.55f * (116.f * fy - 16.f) + .5f,
            500.f * (fx - fy) + .5f,
            200.f * (fy - fz) + .5f};
}

/* The palette in L*a*b*, worked out once on first use rather than by whoever
 * happens to construct something -- it depends on nothing but the list above. */
const std::array<Lab, kColours.size()>& paletteInLab() {
    static const std::array<Lab, kColours.size()> lab = [] {
        std::array<Lab, kColours.size()> out{};
        for (size_t i = 0; i < kColours.size(); i++) out[i] = toLab(kColours[i]);
        return out;
    }();
    return lab;
}

}  // namespace

Rgb Palette::nearest(Rgb colour) {
    const auto& palette = paletteInLab();
    Lab wanted = toLab(colour);
    size_t closest = 0;
    float shortest = -1.f;

    for (size_t i = 0; i < palette.size(); i++) {
        float dl = wanted.l - palette[i].l;
        float da = wanted.a - palette[i].a;
        float db = wanted.b - palette[i].b;
        float distance = dl * dl + da * da + db * db;

        /* Compared squared: the square root is a positive increasing
         * function, so it cannot change which of two is smaller. */
        if (shortest < 0.f || distance < shortest) {
            shortest = distance;
            closest = i;
        }
    }

    return kColours[closest];
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
