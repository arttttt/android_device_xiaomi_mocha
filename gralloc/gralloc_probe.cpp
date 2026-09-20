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

/*
 * Asks for the buffer NVIDIA's gralloc refuses.
 *
 * The quirk and the wrapper that answers it are described in
 * gralloc_tegra.cpp. This asks for the case by hand so that the answer does
 * not depend on finding an application that happens to want it: 640x480 in
 * HAL_PIXEL_FORMAT_YCbCr_420_888 with GRALLOC_USAGE_HW_TEXTURE and nothing
 * else, which is what was seen refused.
 *
 * The request goes the ordinary way -- AHardwareBuffer, GraphicBufferAllocator,
 * the allocator@2.0 service, its passthrough over gralloc0 -- so what it
 * exercises is the stack as it ships, not a shortcut to the blob.
 *
 * Two more cases are asked alongside to show the shape of the rule rather than
 * one bare verdict: the same buffer with a software read, which the blob has
 * always allowed, and the implementation-defined format, which it allows only
 * while software access is absent.
 *
 * Exit status is the number of cases that did not come out as expected, so
 * this can be run from a script.
 */

#include <android/hardware_buffer.h>

#include <inttypes.h>
#include <stdio.h>

namespace {

struct Case {
    const char *what;
    uint32_t format;
    uint64_t usage;
    bool expected;
};

bool ask(const Case &c) {
    AHardwareBuffer_Desc desc = {};
    desc.width = 640;
    desc.height = 480;
    desc.layers = 1;
    desc.format = c.format;
    desc.usage = c.usage;

    AHardwareBuffer *buffer = nullptr;
    int rc = AHardwareBuffer_allocate(&desc, &buffer);

    if (rc == 0 && buffer != nullptr) {
        AHardwareBuffer_release(buffer);
    }

    bool ok = (rc == 0);

    printf("%-46s format %#04x usage %#06" PRIx64 "  ->  %s%s\n", c.what,
           c.format, c.usage, ok ? "allocated" : "refused",
           ok == c.expected ? "" : "   NOT WHAT WAS EXPECTED");

    return ok == c.expected;
}

}  // namespace

int main() {
    const Case cases[] = {
        {"flexible YUV, sampled only", AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420,
         AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, true},

        {"flexible YUV, sampled and read", AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420,
         AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                 AHARDWAREBUFFER_USAGE_CPU_READ_RARELY, true},

        /* HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, which has no public name. */
        {"implementation defined, sampled only", 0x22,
         AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, true},
    };

    int wrong = 0;

    for (const Case &c : cases) {
        if (!ask(c)) {
            wrong++;
        }
    }

    printf("\n%d of %zu came out other than expected\n", wrong,
           sizeof(cases) / sizeof(cases[0]));

    return wrong;
}
