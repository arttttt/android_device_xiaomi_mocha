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
 * The vendor allocator, with two of its answers corrected: an allocation it
 * refuses, below, and an unlock nobody waited for, at tegra_unlock_async.
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
 * that is only ever sampled. The framework draws no such line, and asks for
 * exactly that buffer as a matter of course: every time a Codec2 component is
 * handed a surface, configureProducer() calls getGenerationNumber(), which
 * dequeues one buffer purely to read the generation off it --
 *
 *     Input{640, 480, HAL_PIXEL_FORMAT_YCBCR_420_888, 0}
 *         frameworks/av/media/codec2/vndk/platform/C2BqBuffer.cpp:280
 *
 * -- with no usage of its own, so the queue contributes the consumer's
 * GRALLOC_USAGE_HW_TEXTURE and nothing else. The 640x480 is that constant and
 * not the size of anything being played, which is why the refusal always
 * arrives at that size whatever the video is.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <sync/sync.h>

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

static int (*gVendorLockYCbCr)(const gralloc_module_t *, buffer_handle_t, int,
                               int, int, int, int, struct android_ycbcr *) = NULL;

static int (*gVendorLockAsyncYCbCr)(const gralloc_module_t *, buffer_handle_t,
                                    int, int, int, int, int,
                                    struct android_ycbcr *, int) = NULL;

static int (*gVendorUnlockAsync)(const gralloc_module_t *, buffer_handle_t,
                                 int *) = NULL;

/*
 * The allocator as the vendor wrote it, kept before its pointer is replaced.
 * Reading it back out of the device would read the replacement, and the first
 * allocation would call itself until the stack ran out.
 */
static int (*gVendorAlloc)(alloc_device_t *, int, int, int, int,
                           buffer_handle_t *, int *) = NULL;

/*
 * Every allocation, when persist.mocha.gralloc.trace is set. Off by default
 * and read once: what a decoder asks for is otherwise invisible from this
 * side, and guessing at it has already cost a day.
 */
static bool tracing() {
    static int state = -1;

    if (state < 0) {
        char value[PROPERTY_VALUE_MAX];
        property_get("persist.mocha.gralloc.trace", value, "0");
        state = (atoi(value) != 0) ? 1 : 0;
    }

    return state == 1;
}

/*
 * Whether to ask for cacheable memory for buffers a decoder writes with the
 * processor and something else then reads with it.
 *
 * The allocator gives write combining unless the usage mentions the video
 * encoder, and write combining is not ordered against an ordinary read, which
 * made it the first suspect for video thumbnails that came out torn -- the
 * same file clean once and torn the next time.
 *
 * Measured, it is not that: with this on, the thumbnails tore exactly as
 * before. What tore them was an unlock whose fence nobody waited for; see
 * tegra_unlock_async. The switch stays, off by default. The bit itself is the
 * encoder's, which is a blunt way to ask, and the allocator reads it for more
 * than the cache attribute.
 */
static bool wants_cacheable() {
    static int state = -1;

    if (state < 0) {
        char value[PROPERTY_VALUE_MAX];
        property_get("persist.mocha.gralloc.cacheable", value, "0");
        state = (atoi(value) != 0) ? 1 : 0;
    }

    return state == 1;
}

static int tegra_alloc(alloc_device_t *dev, int w, int h, int format, int usage,
                       buffer_handle_t *handle, int *stride) {
    if (wants_cacheable() && format == HAL_PIXEL_FORMAT_YV12 &&
        (usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK)) &&
        (usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) == 0) {
        int asked = usage | GRALLOC_USAGE_HW_VIDEO_ENCODER;

        if (tracing()) {
            ALOGI("asking for cacheable: %dx%d usage %#x -> %#x", w, h, usage,
                  asked);
        }

        usage = asked;
    }

    int result = gVendorAlloc(dev, w, h, format, usage, handle, stride);

    if (tracing()) {
        ALOGI("alloc %dx%d format %#x usage %#x -> %d, stride %d", w, h, format,
              usage, result, (result == 0 && stride != NULL) ? *stride : -1);
    }

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

/*
 * What a lock hands back, when the trace is on. The pitch named here is not
 * the one the allocation reported: this vendor keeps a second, pitch linear
 * copy for software access and locks give that one's stride, while alloc
 * gives the block linear original's. A reader that believes one number while
 * the writer used the other gets a picture that slides a little further wrong
 * with every row, so both are worth seeing side by side.
 */
static void trace_lock(const char *what, int w, int h, int usage,
                       const struct android_ycbcr *ycbcr, int result) {
    if (!tracing()) {
        return;
    }

    if (result != 0 || ycbcr == NULL || ycbcr->y == NULL) {
        ALOGI("%s %dx%d usage %#x -> %d", what, w, h, usage, result);
        return;
    }

    const char *y = static_cast<const char *>(ycbcr->y);

    ALOGI("%s %dx%d usage %#x -> ystride %zu cstride %zu step %zu "
          "cb%+ld cr%+ld", what, w, h, usage, ycbcr->ystride, ycbcr->cstride,
          ycbcr->chroma_step,
          (long)(static_cast<const char *>(ycbcr->cb) - y),
          (long)(static_cast<const char *>(ycbcr->cr) - y));
}

static int tegra_lock_ycbcr(const gralloc_module_t *module,
                            buffer_handle_t handle, int usage, int l, int t,
                            int w, int h, struct android_ycbcr *ycbcr) {
    int result = gVendorLockYCbCr(module, handle, usage, l, t, w, h, ycbcr);

    trace_lock("lock_ycbcr", w, h, usage, ycbcr, result);

    return result;
}

static int tegra_lock_async_ycbcr(const gralloc_module_t *module,
                                  buffer_handle_t handle, int usage, int l,
                                  int t, int w, int h,
                                  struct android_ycbcr *ycbcr, int fence) {
    int result = gVendorLockAsyncYCbCr(module, handle, usage, l, t, w, h, ycbcr,
                                       fence);

    trace_lock("lockAsync_ycbcr", w, h, usage, ycbcr, result);

    return result;
}

/*
 * An unlock that is finished when it returns.
 *
 * The blob's unlockAsync does not complete the work an unlock owes a buffer the
 * processor has written. It starts it -- a cache sync for the device, and when
 * the buffer has a pitch linear shadow for software access, a 2D blit from the
 * shadow into the real surface -- and hands back a fence that signals when that
 * is done. The passthrough mapper takes this form whenever the module offers
 * it, which this one does (module_api_version 0x0003), and passes the fence up
 * to IMapper::unlock.
 *
 * Whether anything then waits is up to the caller. libui does:
 * GraphicBufferMapper::unlock sync_waits on it before returning. Codec2 does
 * not: C2AllocationGralloc::unmap drops the fence where AOSP left a TODO, and
 * nothing further down waits either. So a software decoder's frame could reach
 * a reader in another process before the unlock behind it had landed, and on
 * this board that is what video thumbnails were made of -- a 2560x1440 clip
 * with about a quarter of the picture showing whatever the block had held
 * before, a different quarter on every run, and never a clip of 1280x720 or
 * less. Waiting for the fence made every frame whole, run after run.
 *
 * Waiting here instead of in Codec2 makes the unlock complete for every
 * caller, including ones that have not been found yet, and leaves AOSP as it
 * is. The price falls on callers that meant to pass the fence on rather than
 * wait -- a software-rendered Surface hands it to queueBuffer -- which now wait
 * on the processor instead.
 *
 * It is real work, not a formality. Measured over 480 unlocks from software
 * decoders building gallery thumbnails: median 4 ms, 90th percentile 16 ms,
 * longest 17.4 ms, none near the timeout. All of them were in media.swcodec,
 * which is where Codec2's own wait used to be, so that path pays what it paid
 * before; no other process met a fence during the measurement.
 *
 * A fence that does not signal in time is handed on as it is, not reported as
 * done: better a caller that waits on it than one told the buffer is ready.
 */
static const int kUnlockWaitMs = 1000;

static int64_t now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int tegra_unlock_async(const gralloc_module_t *module,
                              buffer_handle_t handle, int *fenceFd) {
    int fence = -1;
    int result = gVendorUnlockAsync(module, handle, &fence);

    if (fence >= 0) {
        int64_t start = tracing() ? now_us() : 0;

        if (sync_wait(fence, kUnlockWaitMs) == 0) {
            close(fence);
            fence = -1;

            if (tracing()) {
                ALOGI("unlock fence signalled after %lld us",
                      (long long)(now_us() - start));
            }
        } else {
            ALOGW("unlock fence not signalled in %d ms, handing it on",
                  kUnlockWaitMs);
        }
    }

    if (fenceFd != NULL) {
        *fenceFd = fence;
    } else if (fence >= 0) {
        close(fence);
    }

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

    /*
     * The lock is taken over only to say what it returned, and only when the
     * vendor has one to call. A null stays null: a caller checks for it.
     */
    if (HAL_MODULE_INFO_SYM.base.lock_ycbcr != NULL) {
        gVendorLockYCbCr = HAL_MODULE_INFO_SYM.base.lock_ycbcr;
        HAL_MODULE_INFO_SYM.base.lock_ycbcr = tegra_lock_ycbcr;
    }

    /*
     * The passthrough mapper reaches for the asynchronous form first, so the
     * plain one is never called and tracing it alone says nothing.
     */
    if (HAL_MODULE_INFO_SYM.base.lockAsync_ycbcr != NULL) {
        gVendorLockAsyncYCbCr = HAL_MODULE_INFO_SYM.base.lockAsync_ycbcr;
        HAL_MODULE_INFO_SYM.base.lockAsync_ycbcr = tegra_lock_async_ycbcr;
    }

    /*
     * Only the asynchronous unlock is taken over: it is the one the mapper
     * calls, and the plain one does not hand a fence out to be lost.
     */
    if (HAL_MODULE_INFO_SYM.base.unlockAsync != NULL) {
        gVendorUnlockAsync = HAL_MODULE_INFO_SYM.base.unlockAsync;
        HAL_MODULE_INFO_SYM.base.unlockAsync = tegra_unlock_async;
    }
}
