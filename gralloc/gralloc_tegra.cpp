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
 * The vendor allocator, with one answer of its corrected.
 *
 * gralloc.tegra.so turns down HAL_PIXEL_FORMAT_YCbCr_420_888 when the usage
 * asks for no software access at all:
 *
 *     3516:  tst.w fp, #255   ; usage & (SW_READ_MASK | SW_WRITE_MASK)
 *     351a:  beq   35bc       ; none of them set, so ...
 *     35bc:  mvn.w r0, #21    ; ... -EINVAL
 *
 * Twenty instructions further on it refuses the implementation-defined format
 * when software access *is* asked for. Between the two rules NVIDIA decided
 * which format belongs to which consumer and left none at all for a YUV buffer
 * that is only ever sampled -- which is exactly what Chromium's ImageReader
 * wants, 640x480 in format 35 with GRALLOC_USAGE_HW_TEXTURE and nothing else.
 * The framework draws no such line.
 *
 * So the request is made a second time with the cheapest software read bit
 * set. That bit is a key to the gate and not a property of the memory: the
 * wrapper below it picks the layout from the hardware usage -- HW_TEXTURE gets
 * block linear either way -- and strips the software bits back off before the
 * allocation proper:
 *
 *     3882:  cmp   r1, #1         ; layout == pitch?
 *     388e:  bicne r1, r1, #255   ; no, so drop the software usage
 *
 * The buffer that comes back is therefore the one the caller described, not a
 * software-mapped copy of it.
 *
 * This lives here rather than in a patch to AOSP's passthrough allocator
 * because the quirk is this vendor's, and generic code has nothing wrong with
 * it. The vendor blob is installed as gralloc.nvidia.so and opened by full
 * path; ours answers to gralloc.tegra, which ro.board.platform selects.
 *
 * To take the wrapper out of the way without rebuilding anything, set
 *
 *     ro.hardware.gralloc=nvidia
 *
 * hw_get_module_by_class reads ro.hardware.<class> before it walks the variant
 * keys (hardware/libhardware/hardware.c:219), so that loads the vendor blob
 * directly and this file is never asked anything.
 */

#define LOG_TAG "gralloc.tegra"

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <log/log.h>

static const char kVendorPath[] = "/vendor/lib/hw/gralloc.nvidia.so";

static gralloc_module_t *gVendorModule = NULL;

/*
 * The allocator as the vendor wrote it, kept before its pointer is replaced.
 * Reading it back out of the device would read the replacement, and the first
 * allocation would call itself until the stack ran out.
 */
static int (*gVendorAlloc)(alloc_device_t *, int, int, int, int,
                           buffer_handle_t *, int *) = NULL;

static int tegra_alloc(alloc_device_t *dev, int w, int h, int format, int usage,
                       buffer_handle_t *handle, int *stride) {
    int result = gVendorAlloc(dev, w, h, format, usage, handle, stride);

    if (result != -EINVAL || format != HAL_PIXEL_FORMAT_YCbCr_420_888) {
        return result;
    }

    if (usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK)) {
        return result;
    }

    int retried = usage | GRALLOC_USAGE_SW_READ_RARELY;

    result = gVendorAlloc(dev, w, h, format, retried, handle, stride);

    ALOGI("YCbCr_420_888 %dx%d refused for usage %#x, asked again with a "
          "software read: %d", w, h, usage, result);

    return result;
}

static int tegra_open(const hw_module_t *module __unused, const char *name,
                      hw_device_t **device) {
    if (gVendorModule == NULL) {
        ALOGE("no vendor module to open");
        return -ENODEV;
    }

    int rc = gVendorModule->common.methods->open(&gVendorModule->common, name,
                                                 device);
    if (rc != 0) {
        return rc;
    }

    /*
     * The same open also hands out the framebuffer device, which has no
     * allocator in it and is passed through untouched.
     */
    if (strcmp(name, GRALLOC_HARDWARE_GPU0) != 0) {
        return 0;
    }

    alloc_device_t *allocator = reinterpret_cast<alloc_device_t *>(*device);

    gVendorAlloc = allocator->alloc;
    allocator->alloc = tegra_alloc;

    return 0;
}

static struct hw_module_methods_t tegra_module_methods = {
    .open = tegra_open,
};

gralloc_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = GRALLOC_MODULE_API_VERSION_0_3,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = GRALLOC_HARDWARE_MODULE_ID,
        .name = "Graphics Memory Allocator for Tegra",
        .author = "Artem Bambalov",
        .methods = &tegra_module_methods,
    },
};

/*
 * Everything the module answers other than open is the vendor's, taken as
 * written rather than wrapped in thunks of our own: a pointer this blob leaves
 * null has to stay null, and a thunk over a null pointer is a crash rather
 * than the "not supported" the caller is checking for.
 */
__attribute__((constructor)) static void tegra_take_vendor_module() {
    void *handle = dlopen(kVendorPath, RTLD_NOW | RTLD_LOCAL);

    if (handle == NULL) {
        ALOGE("cannot open %s: %s", kVendorPath, dlerror());
        return;
    }

    gVendorModule = reinterpret_cast<gralloc_module_t *>(
            dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));

    if (gVendorModule == NULL) {
        ALOGE("%s carries no " HAL_MODULE_INFO_SYM_AS_STR, kVendorPath);
        return;
    }

    gralloc_module_t *self = &HAL_MODULE_INFO_SYM;

    self->registerBuffer = gVendorModule->registerBuffer;
    self->unregisterBuffer = gVendorModule->unregisterBuffer;
    self->lock = gVendorModule->lock;
    self->unlock = gVendorModule->unlock;
    self->perform = gVendorModule->perform;
    self->lock_ycbcr = gVendorModule->lock_ycbcr;
    self->lockAsync = gVendorModule->lockAsync;
    self->unlockAsync = gVendorModule->unlockAsync;
    self->lockAsync_ycbcr = gVendorModule->lockAsync_ycbcr;
    self->getTransportSize = gVendorModule->getTransportSize;
    self->validateBufferSize = gVendorModule->validateBufferSize;

    /* Answer for the version the blob actually implements, not for ours. */
    self->common.module_api_version = gVendorModule->common.module_api_version;
}
