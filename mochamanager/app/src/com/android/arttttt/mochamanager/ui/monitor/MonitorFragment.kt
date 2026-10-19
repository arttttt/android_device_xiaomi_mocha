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

package com.android.arttttt.mochamanager.ui.monitor

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.ui.common.Format
import com.android.arttttt.mochamanager.ui.common.Rows
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import java.util.Locale

class MonitorFragment : Fragment() {
    private val model by screenViewModel { MonitorViewModel(it.monitor) }
    private lateinit var rows: Rows
    private var built = false

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View {
        rows = Rows(requireContext())
        rows.section(R.string.section_cpu)
        rows.row(CLUSTER, R.string.cluster)
        built = false
        return rows.root
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        collectWhileShown(model.state, ::render)
    }

    private fun render(state: Loadable<Snapshot>) {
        val s = when (state) {
            is Loadable.Ready -> state.value
            Loadable.Loading -> return rows.set(CLUSTER, getString(R.string.loading))
            Loadable.Unavailable -> return rows.set(CLUSTER, getString(R.string.service_unavailable))
        }
        /* How many cores and zones there are is the service's to say. */
        if (!built) build(s)

        rows[CLUSTER] = s.cluster
        for (c in s.cores) {
            rows["core${c.id}"] = if (c.online) {
                Format.mhz(c.curKhz) + " (" + Format.mhz(c.minKhz) + "–" + Format.mhz(c.maxKhz) +
                        ", " + c.governor + ")"
            } else getString(R.string.core_offline)
        }
        rows[GPU_FREQ] = Format.mhzHz(s.gpu.curHz) + " / " + Format.mhzHz(s.gpu.maxHz)
        rows[GPU_LOAD] = Format.percent(s.gpu.loadPermille / 10.0)
        rows[GPU_RAIL] = getString(if (s.gpu.railgated) R.string.gpu_rail_off else R.string.gpu_rail_on)
        rows[EMC] = Format.mhzHz(s.emcHz)
        for (t in s.thermals) rows["thermal_" + t.name] = Format.celsius(t.milliCelsius)

        val m = s.memory
        rows[MEM_AVAILABLE] = Format.mib(m.availableKb) + " / " + Format.mib(m.totalKb)
        rows[MEM_NVMAP] = Format.mib(m.nvmapUsedKb)
        rows[SWAP] = Format.mib(m.swapUsedKb) + " / " + Format.mib(m.swapTotalKb)
        rows[ZRAM_RATIO] = m.zramRatio?.let(Format::ratio) ?: getString(R.string.unknown)
        rows[PSI] = String.format(Locale.getDefault(), "%.2f / %.2f", m.psiSome, m.psiFull)
    }

    private fun build(s: Snapshot) {
        for (c in s.cores) rows.row("core${c.id}", getString(R.string.core, c.id))
        rows.section(R.string.section_gpu)
        rows.row(GPU_FREQ, R.string.gpu_freq)
        rows.row(GPU_LOAD, R.string.gpu_load)
        rows.row(GPU_RAIL, R.string.gpu_rail)
        rows.row(EMC, R.string.emc)
        rows.section(R.string.section_thermal)
        for (t in s.thermals) rows.row("thermal_" + t.name, t.name)
        rows.section(R.string.section_memory)
        rows.row(MEM_AVAILABLE, R.string.mem_available)
        rows.row(MEM_NVMAP, R.string.mem_nvmap)
        rows.row(SWAP, R.string.swap)
        rows.row(ZRAM_RATIO, R.string.zram_ratio)
        rows.row(PSI, R.string.psi)
        built = true
    }

    private companion object {
        const val CLUSTER = "cluster"
        const val GPU_FREQ = "gpu_freq"
        const val GPU_LOAD = "gpu_load"
        const val GPU_RAIL = "gpu_rail"
        const val EMC = "emc"
        const val MEM_AVAILABLE = "mem_available"
        const val MEM_NVMAP = "mem_nvmap"
        const val SWAP = "swap"
        const val ZRAM_RATIO = "zram_ratio"
        const val PSI = "psi"
    }
}
