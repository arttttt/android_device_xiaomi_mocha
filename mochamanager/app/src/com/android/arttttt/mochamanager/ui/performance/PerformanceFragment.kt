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

package com.android.arttttt.mochamanager.ui.performance

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.GridLayout
import android.widget.TextView
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.domain.usecase.thermalLevel
import com.android.arttttt.mochamanager.ui.common.CardTitle
import com.android.arttttt.mochamanager.ui.common.SparklineView
import com.android.arttttt.mochamanager.ui.common.Tile
import com.android.arttttt.mochamanager.ui.common.Units
import com.android.arttttt.mochamanager.ui.common.addRow
import com.android.arttttt.mochamanager.ui.common.cardTitle
import com.android.arttttt.mochamanager.ui.common.chip
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.fill
import com.android.arttttt.mochamanager.ui.common.header
import com.android.arttttt.mochamanager.ui.common.levelColor
import com.android.arttttt.mochamanager.ui.common.roll
import com.android.arttttt.mochamanager.ui.common.settle
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.subtitle
import com.google.android.material.progressindicator.LinearProgressIndicator
import java.util.Locale

class PerformanceFragment : Fragment(R.layout.fragment_performance) {
    private val model by screenViewModel { PerformanceViewModel(it.monitor) }
    private var views: Views? = null

    private class Core(val name: TextView, val bar: LinearProgressIndicator, val value: TextView)
    private class Zone(val root: View, val value: TextView, val name: TextView)

    private class Views(val root: View) {
        val units = Units(root.context)
        val cpuTitle = CardTitle(root.findViewById(R.id.cpu_title)).apply { set(R.string.section_cpu) }
        val cores: ViewGroup = root.findViewById(R.id.cores)
        val spark: SparklineView = root.findViewById<SparklineView>(R.id.cpu_spark).apply {
            periodMs = PerformanceViewModel.PERIOD_MS
        }
        val clusterG: View = root.findViewById(R.id.cluster_g)
        val clusterGState: TextView = root.findViewById(R.id.cluster_g_state)
        val clusterLp: View = root.findViewById(R.id.cluster_lp)
        val clusterLpState: TextView = root.findViewById(R.id.cluster_lp_state)
        val clusterTime: View = root.findViewById(R.id.cluster_time)
        val clusterTimeValue: TextView = root.findViewById(R.id.cluster_time_value)
        val clusterTimeBar: LinearProgressIndicator = root.findViewById(R.id.cluster_time_bar)
        val clusterSwitches: TextView = root.findViewById(R.id.cluster_switches)
        val gpu = Tile(root, R.id.tile_gpu, R.string.section_gpu)
        val emc = Tile(root, R.id.tile_emc, R.string.section_emc)
        val temps: GridLayout = root.findViewById(R.id.temps)
        val memoryBar: LinearProgressIndicator = root.findViewById(R.id.memory_bar)
        val zramBar: LinearProgressIndicator = root.findViewById(R.id.zram_bar)
        val free = root.findViewById<ViewGroup>(R.id.memory_free).addRow(root.context.getString(R.string.mem_available))
        val zram = root.findViewById<ViewGroup>(R.id.memory_zram).addRow(root.context.getString(R.string.swap))
        private val rows: ViewGroup = root.findViewById(R.id.memory_rows)
        val nvmap = rows.addRow(root.context.getString(R.string.mem_nvmap))
        val psi = rows.addRow(root.context.getString(R.string.psi))
        /* As many as the service reports, made when it first does. */
        val coreViews = ArrayList<Core>()
        val zoneViews = ArrayList<Zone>()

        init {
            gpu.bar.visibility = View.VISIBLE
            root.cardTitle(R.id.thermal_title, R.string.section_thermal)
            root.cardTitle(R.id.memory_title, R.string.section_memory)
        }
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        view.header(R.string.title_performance)
        views = Views(view)
        collectWhileShown(model.state, ::render)
    }

    override fun onDestroyView() {
        views = null
        super.onDestroyView()
    }

    private fun render(state: PerformanceState) {
        val v = views ?: return
        val s = state.snapshot.valueOrNull ?: return
        val u = v.units
        v.root.subtitle(getString(R.string.performance_subtitle, s.cluster))
        renderCores(v, s)
        renderClusters(v, s)
        /* On the cores' own range, so the scale seldom moves: only to take in
         * a minute-old clock above a ceiling since lowered. */
        val ceiling = s.cores.maxOfOrNull { it.maxKhz }?.takeIf { it > 0 }?.toFloat()
        v.spark.submit(state.cpu, floor = 0f, ceiling = ceiling)

        v.gpu.value.roll(s.gpu.curHz.toDouble()) { u.mhzOfHz(it) }
        val gated = if (s.gpu.railgated) " · " + getString(R.string.gpu_rail_off) else ""
        v.gpu.sub.roll(doubleArrayOf(s.gpu.maxHz.toDouble(), s.gpu.loadPermille / 10.0)) {
            getString(R.string.gpu_sub, u.mhzOfHz(it[0]), u.percent(it[1])) + gated
        }
        v.gpu.bar.fill(s.gpu.loadPermille / 1000.0)
        v.emc.value.roll(s.emcHz.toDouble()) { u.mhzOfHz(it) }
        v.emc.sub.setText(R.string.emc_sub)

        renderZones(v, s)

        val m = s.memory
        v.free.roll(doubleArrayOf(m.availableKb.toDouble(), m.totalKb.toDouble())) { u.mb(it[0]) + " / " + u.mb(it[1]) }
        v.memoryBar.fill(m.usedFraction)
        val ratio = m.zramRatio?.let { " · " + getString(R.string.zram_ratio, u.ratio(it)) } ?: ""
        v.zram.roll(doubleArrayOf(m.swapUsedKb.toDouble(), m.swapTotalKb.toDouble())) {
            u.mb(it[0]) + " / " + u.mb(it[1]) + ratio
        }
        v.zramBar.fill(m.swapFraction)
        v.nvmap.roll(m.nvmapUsedKb.toDouble()) { u.mb(it) }
        v.psi.roll(doubleArrayOf(m.psiSome.toDouble(), m.psiFull.toDouble())) {
            String.format(Locale.getDefault(), "%.2f / %.2f", it[0], it[1])
        }
    }

    private fun renderCores(v: Views, s: Snapshot) {
        val u = v.units
        /* On LP the G cores are not off but gone, cluster and all: the one
         * core there is runs as core 0. */
        val cores = if (s.onLp) s.cores.take(1) else s.cores
        if (v.coreViews.size != cores.size) {
            v.cores.removeAllViews()
            v.coreViews.clear()
            for (c in cores) {
                val row = layoutInflater.inflate(R.layout.item_core, v.cores, false)
                v.cores.addView(row)
                v.coreViews.add(Core(row.findViewById(R.id.core_name), row.findViewById(R.id.core_bar),
                        row.findViewById(R.id.core_value)))
            }
        }
        for ((i, c) in cores.withIndex()) {
            val core = v.coreViews[i]
            core.name.text = if (s.onLp) getString(R.string.core_lp) else getString(R.string.core, c.id)
            core.bar.fill(c.load.toDouble())
            if (c.online) core.value.roll(c.curKhz.toDouble()) { u.mhz(it) }
            else core.value.settle(getString(R.string.core_offline))
        }
        s.cores.firstOrNull { it.online }?.let { v.cpuTitle.aside.chip(it.governor, null) }
    }

    /* Which cluster runs, and how long each has, when the kernel counts it. */
    private fun renderClusters(v: Views, s: Snapshot) {
        v.clusterG.setBackgroundResource(if (s.onLp) R.drawable.bg_plate else R.drawable.bg_plate_on)
        v.clusterLp.setBackgroundResource(if (s.onLp) R.drawable.bg_plate_on else R.drawable.bg_plate)
        v.clusterGState.setText(if (s.onLp) R.string.cluster_g_off else R.string.cluster_g_on)
        v.clusterLpState.setText(if (s.onLp) R.string.cluster_lp_on else R.string.cluster_lp_off)

        val t = s.clusterTime
        v.clusterTime.visibility = if (t == null) View.GONE else View.VISIBLE
        if (t == null) return
        v.clusterTimeValue.text = getString(R.string.cluster_time_value,
                v.units.duration(t.gMs), v.units.duration(t.lpMs))
        v.clusterTimeBar.fill(1.0 - t.lpShare)
        v.clusterSwitches.text = resources.getQuantityString(R.plurals.cluster_switches, t.switches, t.switches)
    }

    /* A tile per zone, three to a row, coloured by how hot it runs. */
    private fun renderZones(v: Views, s: Snapshot) {
        if (v.zoneViews.size != s.thermals.size) {
            v.temps.removeAllViews()
            v.zoneViews.clear()
            for (t in s.thermals) {
                val tile = LayoutInflater.from(v.temps.context).inflate(R.layout.item_temp, v.temps, false)
                tile.layoutParams = GridLayout.LayoutParams(
                        GridLayout.spec(GridLayout.UNDEFINED), GridLayout.spec(GridLayout.UNDEFINED, 1f)).apply {
                    width = 0
                    val m = (4 * resources.displayMetrics.density).toInt()
                    setMargins(m, m, m, m)
                }
                v.temps.addView(tile)
                v.zoneViews.add(Zone(tile, tile.findViewById(R.id.temp_value), tile.findViewById(R.id.temp_name)))
            }
        }
        for ((i, t) in s.thermals.withIndex()) {
            val zone = v.zoneViews[i]
            val level = thermalLevel(t.milliCelsius)
            zone.value.roll(t.milliCelsius.toDouble()) { v.units.degreesShort(it) }
            zone.value.setTextColor(zone.value.levelColor(level))
            zone.name.text = t.name
        }
    }
}
