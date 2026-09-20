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
 *
 * Why the whole module is copied rather than a module of our own filled in
 * -----------------------------------------------------------------------
 *
 * What the blob exports as HMI is not a gralloc_module_t. It is NVIDIA's
 * NvGrModule: the gralloc module, and then a table of its own that the rest of
 * the Tegra stack reaches by casting whatever hw_get_module() handed back --
 *
 *     +176 NvGrModuleRef          +216 NvGrSetCompressed
 *     +180 NvGrModuleUnref        +220 NvGrGetCompressed
 *     +184 NvGrAllocInternal      +224 NvGrDecompressBuffer
 *     +188 NvGrFreeInternal       +228 NvGrDecompressPrepare
 *     +192 NvGrAddFence           +232 NvGrDecompressCommit
 *     +196 NvGrGetFenceFd         +236 NvGrSetHint
 *     +200 NvGrAddFenceFd         +240 NvGrClearHint
 *     +204 NvGrDumpBuffer         +244 NvGrGetHints
 *     +208 NvGrScratchOpen        +248 NvGrOverrideProperty
 *     +212 NvGrScratchClose
 *
 * -- followed by state the blob keeps there, out to 368 bytes, which is the
 * whole of its .data (0xe000, size 0x178, with HMI at 0xe008). A module of our
 * own would be 176 bytes, and the first caller after a fence would read past
 * the end of it.
 *
 * So the object below is 368 bytes and the vendor's is copied into it whole.
 * That is sound here because the blob never takes the address of its own HMI
 * -- no relocation and no literal in .text points into it -- so every one of
 * its functions works through the module pointer it is handed, which is this
 * copy and never the original. The two cannot drift apart because only one of
 * them is ever used.
 *
 * The layouts line up as well: this tree's gralloc_module_t is 176 bytes and
 * ends with getTransportSize, validateBufferSize and reserved_proc[1], where
 * the blob's older header had reserved_proc[3] and left all three null. Copying
 * leaves those null, which is what a caller checks for.
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

/* sizeof(NvGrModule), read off the blob's .data. See the note above. */
static const size_t kVendorModuleSize = 368;

struct NvGrModule {
    gralloc_module_t base;
    unsigned char nvidia[kVendorModuleSize - sizeof(gralloc_module_t)];
};

static_assert(sizeof(gralloc_module_t) <= kVendorModuleSize,
              "the framework's gralloc module no longer fits inside NvGrModule");

/*
 * Zero until the constructor fills it in. A module left zero has no
 * HARDWARE_MODULE_TAG, so hw_get_module turns the caller away rather than
 * handing out something half built.
 */
struct NvGrModule HAL_MODULE_INFO_SYM;

static int (*gVendorOpen)(const hw_module_t *, const char *, hw_device_t **);

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

static int tegra_open(const hw_module_t *module, const char *name,
                      hw_device_t **device) {
    if (gVendorOpen == NULL) {
        ALOGE("no vendor module to open");
        return -ENODEV;
    }

    /*
     * The module handed on is ours, which is the copy every caller has, so the
     * blob keeps its state in the one object that is in use.
     */
    int rc = gVendorOpen(module, name, device);
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

    /*
     * A second open may hand back the same device, already replaced. Reading
     * the pointer then would save our own function as the vendor's and the
     * next allocation would call itself, so the replacement is made once.
     */
    if (allocator->alloc == tegra_alloc) {
        return 0;
    }

    gVendorAlloc = allocator->alloc;
    allocator->alloc = tegra_alloc;

    return 0;
}

static struct hw_module_methods_t tegra_module_methods = {
    .open = tegra_open,
};

__attribute__((constructor)) static void tegra_take_vendor_module() {
    void *handle = dlopen(kVendorPath, RTLD_NOW | RTLD_LOCAL);

    if (handle == NULL) {
        ALOGE("cannot open %s: %s", kVendorPath, dlerror());
        return;
    }

    const struct NvGrModule *vendor = reinterpret_cast<const struct NvGrModule *>(
            dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));

    if (vendor == NULL) {
        ALOGE("%s carries no " HAL_MODULE_INFO_SYM_AS_STR, kVendorPath);
        return;
    }

    if (vendor->base.common.tag != HARDWARE_MODULE_TAG ||
        vendor->base.common.methods == NULL ||
        vendor->base.common.methods->open == NULL) {
        ALOGE("%s is not a hardware module we can stand in front of",
              kVendorPath);
        return;
    }

    memcpy(&HAL_MODULE_INFO_SYM, vendor, kVendorModuleSize);

    gVendorOpen = HAL_MODULE_INFO_SYM.base.common.methods->open;
    HAL_MODULE_INFO_SYM.base.common.methods = &tegra_module_methods;
}
