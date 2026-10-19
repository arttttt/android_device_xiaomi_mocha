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

package com.android.arttttt.mochamanager.domain

import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.Memory
import com.android.arttttt.mochamanager.domain.model.PerformanceProfile
import com.android.arttttt.mochamanager.domain.model.RefreshMode
import com.android.arttttt.mochamanager.domain.usecase.PowerHistory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ModelsTest {
    @Test
    fun memoryVariant() {
        fun info(kb: Long) = DeviceInfo("MI PAD", "", "", "", kb, null)
        assertEquals(2, info(1967068).memoryGb)
        assertEquals(3, info(2900000).memoryGb)
    }

    @Test
    fun zramRatioOnlyOverSomething() {
        fun memory(orig: Long, compr: Long) = Memory(0, 0, 0, 0, 0, orig, compr, 0f, 0f)
        assertNull(memory(1000, 100).zramRatio)
        assertEquals(4.0, memory(4L shl 20, 1L shl 20).zramRatio!!, 1e-9)
    }

    @Test
    fun refreshModesRoundTrip() {
        for (mode in RefreshMode.values()) assertEquals(mode, RefreshMode.of(mode.min, mode.peak))
        assertNull(RefreshMode.of(48f, 90f))
    }

    @Test
    fun profileCodes() {
        for (p in PerformanceProfile.values()) assertEquals(p, PerformanceProfile.of(p.code))
        assertNull(PerformanceProfile.of(7))
    }

    @Test
    fun historyKeepsTwoMinutes() {
        var h = PowerHistory.EMPTY
        repeat(130) { h = h.plus(it) }
        assertEquals(PowerHistory.CAPACITY, h.samplesMw.size)
        assertEquals(10, h.samplesMw.first())
        assertEquals(129, h.samplesMw.last())
        assertEquals(130L, h.serial)
    }
}
