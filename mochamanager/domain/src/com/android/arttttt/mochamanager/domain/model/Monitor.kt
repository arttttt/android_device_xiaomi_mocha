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

package com.android.arttttt.mochamanager.domain.model

data class CpuCore(
    val id: Int,
    val online: Boolean,
    val curKhz: Int,
    val minKhz: Int,
    val maxKhz: Int,
    val governor: String,
)

data class Gpu(val curHz: Long, val maxHz: Long, val loadPermille: Int, val railgated: Boolean)

data class ThermalZone(val name: String, val milliCelsius: Int)

data class Memory(
    val totalKb: Long,
    val availableKb: Long,
    val nvmapUsedKb: Long,
    val swapTotalKb: Long,
    val swapFreeKb: Long,
    val zramOrigBytes: Long,
    val zramComprBytes: Long,
    val psiSome: Float,
    val psiFull: Float,
) {
    val swapUsedKb: Long
        get() = swapTotalKb - swapFreeKb

    /* Over a nearly empty device the ratio says nothing. */
    val zramRatio: Double?
        get() = if (zramOrigBytes >= 1L shl 20 && zramComprBytes > 0) {
            zramOrigBytes.toDouble() / zramComprBytes
        } else null
}

/* Where the tablet's effort goes right now. */
data class Snapshot(
    val cluster: String,
    val cores: List<CpuCore>,
    val gpu: Gpu,
    val emcHz: Long,
    val thermals: List<ThermalZone>,
    val memory: Memory,
)
