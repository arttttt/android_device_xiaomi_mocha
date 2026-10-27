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

import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.ClusterTime
import com.android.arttttt.mochamanager.domain.model.CpuCore
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.Gpu
import com.android.arttttt.mochamanager.domain.model.Memory
import com.android.arttttt.mochamanager.domain.model.PerformanceProfile
import com.android.arttttt.mochamanager.domain.model.PowerMode
import com.android.arttttt.mochamanager.domain.model.RefreshMode
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.domain.model.Supply
import com.android.arttttt.mochamanager.domain.model.ThermalZone
import com.android.arttttt.mochamanager.domain.usecase.History
import com.android.arttttt.mochamanager.domain.usecase.Level
import com.android.arttttt.mochamanager.domain.usecase.chargeLevel
import com.android.arttttt.mochamanager.domain.usecase.thermalLevel
import com.android.arttttt.mochamanager.domain.usecase.wearLevel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
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
    fun historyKeepsItsCapacity() {
        var h = History.empty(120)
        repeat(130) { h = h.plus(it.toFloat()) }
        assertEquals(120, h.samples.size)
        assertEquals(10f, h.samples.first(), 0f)
        assertEquals(129f, h.samples.last(), 0f)
        assertEquals(130L, h.serial)
    }

    @Test
    fun levels() {
        assertEquals(Level.GOOD, thermalLevel(41_000))
        assertEquals(Level.WARN, thermalLevel(47_250))
        assertEquals(Level.BAD, thermalLevel(70_000))
        assertEquals(Level.GOOD, wearLevel(92.0))
        assertEquals(Level.WARN, wearLevel(72.0))
        assertEquals(Level.BAD, wearLevel(50.0))
        assertEquals(Level.GOOD, chargeLevel(92))
        assertEquals(Level.WARN, chargeLevel(20))
        assertEquals(Level.BAD, chargeLevel(15))
    }

    @Test
    fun snapshotSummary() {
        val memory = Memory(2000, 500, 0, 1000, 750, 0, 0, 0f, 0f)
        val s = Snapshot("G",
                listOf(CpuCore(0, true, 204_000, 51_000, 2_116_500, "interactive"),
                        CpuCore(1, true, 828_000, 51_000, 2_116_500, "interactive"),
                        CpuCore(2, false, 0, 0, 0, "")),
                Gpu(72_000_000, 852_000_000, 40, false), 528_000_000,
                listOf(ThermalZone("CPU", 41_000), ThermalZone("Tdiode", 47_250)), memory, null)
        assertEquals(828_000, s.busiestKhz)
        assertFalse(s.onLp)
        assertEquals("Tdiode", s.hottest!!.name)
        assertEquals(0f, s.cores[2].load, 0f)
        assertEquals(0.75, memory.usedFraction, 1e-9)
        assertEquals(0.25, memory.swapFraction, 1e-9)
    }

    @Test
    fun powerMode() {
        fun battery(disabled: Boolean, open: Boolean) =
                Battery(80, "Discharging", "Good", 4_000_000, 400_000, 300, 3_000_000, 3_800_000,
                        4_000_000, 10, Supply.USB, disabled, open)
        assertEquals(PowerMode.CHARGE, battery(disabled = false, open = false).powerMode)
        assertEquals(PowerMode.HOLD, battery(disabled = true, open = false).powerMode)
        /* An open input runs the tablet on the battery whatever the switch. */
        assertEquals(PowerMode.BATTERY, battery(disabled = true, open = true).powerMode)
        assertEquals(PowerMode.BATTERY, battery(disabled = false, open = true).powerMode)
    }

    @Test
    fun clusterTime() {
        assertEquals(0.25, ClusterTime(3_000, 1_000, 6).lpShare, 1e-9)
        assertEquals(0.0, ClusterTime(0, 0, 0).lpShare, 0.0)
    }
}
