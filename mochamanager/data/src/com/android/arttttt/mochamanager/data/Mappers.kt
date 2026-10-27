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

package com.android.arttttt.mochamanager.data

import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.ClusterTime
import com.android.arttttt.mochamanager.domain.model.CpuCore
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.Gpu
import com.android.arttttt.mochamanager.domain.model.Memory
import com.android.arttttt.mochamanager.domain.model.PowerSample
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.domain.model.Supply
import com.android.arttttt.mochamanager.domain.model.ThermalZone
import vendor.arttttt.mochamanager.AccelCalibration as AidlCalibration
import vendor.arttttt.mochamanager.BatteryInfo as AidlBattery
import vendor.arttttt.mochamanager.DeviceInfo as AidlDevice
import vendor.arttttt.mochamanager.PowerSample as AidlPower
import vendor.arttttt.mochamanager.Snapshot as AidlSnapshot

/* The service's parcelables as the domain's models: the IDL stops here. */

internal fun AidlDevice.toDomain(serial: String?) =
        DeviceInfo(model, build, kernel, wifi, memTotalKb, serial)

internal fun AidlPower.toDomain() =
        PowerSample(voltageMv, currentMa, powerMw, energyUWs.takeIf { it >= 0 })

internal fun AidlBattery.toDomain() = Battery(
        capacityPct, status, health, voltageUv, currentUa, tempDeciC, chargeNowUah,
        chargeFullUah, chargeFullDesignUah, cycleCount,
        when {
            acOnline -> Supply.AC
            usbOnline -> Supply.USB
            else -> Supply.NONE
        },
        chargingDisabled,
        inputOpen)

internal fun AidlSnapshot.toDomain() = Snapshot(
        cluster,
        cores.map { CpuCore(it.id, it.online, it.curKhz, it.minKhz, it.maxKhz, it.governor) },
        Gpu(gpu.curHz, gpu.maxHz, gpu.loadPermille, gpu.railgated),
        emcHz,
        thermals.map { ThermalZone(it.name, it.milliCelsius) }.sortedBy { it.name },
        memory.let {
            Memory(it.totalKb, it.availableKb, it.nvmapUsedKb, it.swapTotalKb, it.swapFreeKb,
                    it.zramOrigBytes, it.zramComprBytes, it.psiSome, it.psiFull)
        },
        clusterTime.takeIf { it.present }?.let { ClusterTime(it.gMs, it.lpMs, it.switches) })

internal fun AidlCalibration.toDomain() =
        AccelCalibration(present, offset.toList(), scale.toList(), madeMs)

internal fun AccelCalibration.toAidl() = AidlCalibration().also {
    it.present = present
    it.offset = offset.toIntArray()
    it.scale = scale.toFloatArray()
    it.madeMs = madeMs
}
