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
#define LOG_TAG "android.hardware.power-service.mocha"

#include "CameraHints.h"

#include <algorithm>
#include <cstdio>

#include <log/log.h>

namespace aidl::android::hardware::power::impl::mocha {

namespace {

struct Floors {
    uint32_t cpuKhz;
    uint32_t gpuKhz;
};

/* Provisional, until the camera's measurement of its pipeline's headroom
 * says what each load needs: the preview's statistics and demosaic miss
 * their time at the clocks the governor leaves them, and these are steps
 * of this board's tables that clear it with room. Always a pair: the
 * statistics and 3A run on the processor, demosaic on the GPU, and a floor
 * under one alone leaves the frame waiting on the other.
 *
 * Memory is not asked for. The kernel raises the EMC clock by itself from
 * the processor's and the GPU's clocks and the bandwidth they claim, so a
 * floor under both brings memory up with them. */
constexpr std::array<Floors, CameraHints::LEVELS> STREAMING = {{
        {696000, 252000},   /* up to 720p at 30 */
        {1044000, 396000},  /* 1080p at 30 */
        {1530000, 540000},  /* above that */
}};

/* What a launch or a shot asks for when it does not say how long. */
constexpr int32_t DEFAULT_BOOST_MS = 1000;

constexpr char CPU_CEILING[] = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq";
constexpr char GPU_CEILING[] = "/sys/kernel/tegra_gpu/gpu_cap_rate";

/* A number read from sysfs, nought if there is none. */
uint64_t readNumber(const char* path) {
    FILE* f = fopen(path, "re");
    if (!f) return 0;
    unsigned long long value = 0;
    if (fscanf(f, "%llu", &value) != 1) value = 0;
    fclose(f);
    return value;
}

/* The profile's ceilings, in kilohertz; nought where one cannot be read. */
uint32_t cpuCeilingKhz() {
    return static_cast<uint32_t>(readNumber(CPU_CEILING));
}

uint32_t gpuCeilingKhz() {
    return static_cast<uint32_t>(readNumber(GPU_CEILING) / 1000);
}

uint32_t underCeiling(uint32_t khz, uint32_t ceiling) {
    return ceiling != 0 ? std::min(khz, ceiling) : khz;
}

}  // namespace

CameraHints::CameraHints() : mTimer(&CameraHints::timerLoop, this) {}

CameraHints::~CameraHints() {
    {
        std::lock_guard<std::mutex> lock(mLock);
        mQuit = true;
    }
    mWake.notify_all();
    mTimer.join();
}

void CameraHints::stream(Level level, bool on) {
    if (static_cast<int>(level) >= LEVELS) return;

    std::lock_guard<std::mutex> lock(mLock);
    mStreaming[level] = on;
    applyStreamLocked();
}

void CameraHints::boost(int32_t durationMs) {
    if (durationMs <= 0) durationMs = DEFAULT_BOOST_MS;

    std::lock_guard<std::mutex> lock(mLock);
    if (mSuppressed) return;

    /* Up to the ceiling: an opening camera or a shot is the user waiting
     * on the device, as much as a touch is. */
    const uint32_t cpu = cpuCeilingKhz();
    const uint32_t gpu = gpuCeilingKhz();
    mBoostCpu.set(cpu != 0 ? cpu : STREAMING[HIGH].cpuKhz);
    mBoostGpu.set(gpu != 0 ? gpu : STREAMING[HIGH].gpuKhz);

    /* Bursts that overlap run to the later end, never cut the earlier. */
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
    if (!mBoosting || until > mBoostUntil) mBoostUntil = until;
    mBoosting = true;
    mWake.notify_all();
}

void CameraHints::setSuppressed(bool suppressed) {
    std::lock_guard<std::mutex> lock(mLock);
    if (suppressed == mSuppressed) return;

    mSuppressed = suppressed;
    if (suppressed) dropBoostLocked();
    applyStreamLocked();
}

void CameraHints::reset() {
    std::lock_guard<std::mutex> lock(mLock);

    const bool held = mBoosting ||
                      std::any_of(mStreaming.begin(), mStreaming.end(), [](bool on) { return on; });
    if (held) ALOGI("camera provider gone; letting go of its floors");

    mStreaming.fill(false);
    dropBoostLocked();
    applyStreamLocked();
}

void CameraHints::applyStreamLocked() {
    /* More than one level at once is more than one camera: the heaviest
     * one decides. */
    int level = -1;
    for (int i = LEVELS - 1; i >= 0; i--) {
        if (mStreaming[i]) {
            level = i;
            break;
        }
    }

    if (level < 0 || mSuppressed) {
        mCpu.set(0);
        mGpu.set(0);
        return;
    }

    const Floors& want = STREAMING[level];
    mCpu.set(underCeiling(want.cpuKhz, cpuCeilingKhz()));
    mGpu.set(underCeiling(want.gpuKhz, gpuCeilingKhz()));
}

void CameraHints::dropBoostLocked() {
    mBoosting = false;
    mBoostCpu.set(0);
    mBoostGpu.set(0);
}

void CameraHints::timerLoop() {
    std::unique_lock<std::mutex> lock(mLock);
    while (!mQuit) {
        if (!mBoosting) {
            mWake.wait(lock);
            continue;
        }
        /* Woken early by a burst that moves the end, or by the end. */
        if (mWake.wait_until(lock, mBoostUntil) == std::cv_status::timeout &&
            mBoosting && std::chrono::steady_clock::now() >= mBoostUntil) {
            dropBoostLocked();
        }
    }
}

}  // namespace aidl::android::hardware::power::impl::mocha
