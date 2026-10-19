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

package com.android.arttttt.mochamanager

/* Where the tablet's effort goes right now. */
class MonitorFragment : Screen(2000) {

    override fun build() {
        rows.section(R.string.section_cpu)
        rows.row("cluster", R.string.cluster)
        /* The cores and the thermal zones are added on the first reading:
         * how many there are is the service's to say. */
    }

    override fun refresh() {
        Service.load({ it.readSnapshot() }) { s ->
            if (s == null) return@load unavailable()
            if (!rows.has("emc")) build(s)

            rows["cluster"] = s.cluster
            for (c in s.cores) {
                rows["core${c.id}"] = if (c.online) {
                    Format.mhz(c.curKhz) + " (" + Format.mhz(c.minKhz) + "–" + Format.mhz(c.maxKhz) +
                            ", " + c.governor + ")"
                } else getString(R.string.core_offline)
            }
            rows["gpu_freq"] = Format.mhzHz(s.gpu.curHz) + " / " + Format.mhzHz(s.gpu.maxHz)
            rows["gpu_load"] = Format.percent(s.gpu.loadPermille / 10.0)
            rows["gpu_rail"] = getString(if (s.gpu.railgated) R.string.gpu_rail_off else R.string.gpu_rail_on)
            rows["emc"] = Format.mhzHz(s.emcHz)
            for (t in s.thermals) rows["thermal_" + t.name] = Format.celsius(t.milliCelsius)

            val m = s.memory
            rows["mem_available"] = Format.mib(m.availableKb) + " / " + Format.mib(m.totalKb)
            rows["mem_nvmap"] = Format.mib(m.nvmapUsedKb)
            rows["swap"] = Format.mib(m.swapTotalKb - m.swapFreeKb) + " / " + Format.mib(m.swapTotalKb)
            rows["zram_ratio"] = if (m.zramComprBytes > 0) {
                Format.ratio(m.zramOrigBytes.toDouble() / m.zramComprBytes)
            } else getString(R.string.unknown)
            rows["psi"] = String.format("%.2f / %.2f", m.psiSome, m.psiFull)
        }
    }

    private fun build(s: vendor.arttttt.mochamanager.Snapshot) {
        for (c in s.cores) rows.row("core${c.id}", getString(R.string.core, c.id))
        rows.section(R.string.section_gpu)
        rows.row("gpu_freq", R.string.gpu_freq)
        rows.row("gpu_load", R.string.gpu_load)
        rows.row("gpu_rail", R.string.gpu_rail)
        rows.row("emc", R.string.emc)
        rows.section(R.string.section_thermal)
        for (t in s.thermals.sortedBy { it.name }) rows.row("thermal_" + t.name, t.name)
        rows.section(R.string.section_memory)
        rows.row("mem_available", R.string.mem_available)
        rows.row("mem_nvmap", R.string.mem_nvmap)
        rows.row("swap", R.string.swap)
        rows.row("zram_ratio", R.string.zram_ratio)
        rows.row("psi", R.string.psi)
    }
}
