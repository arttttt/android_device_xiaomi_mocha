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

package com.android.arttttt.mochamanager.ui.overview

import android.os.Bundle
import android.os.SystemProperties
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.domain.usecase.chargeLevel
import com.android.arttttt.mochamanager.domain.usecase.thermalLevel
import com.android.arttttt.mochamanager.domain.usecase.wearLevel
import com.android.arttttt.mochamanager.ui.common.CardTitle
import com.android.arttttt.mochamanager.ui.common.Tile
import com.android.arttttt.mochamanager.ui.common.Units
import com.android.arttttt.mochamanager.ui.common.addRow
import com.android.arttttt.mochamanager.ui.common.chip
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.fill
import com.android.arttttt.mochamanager.ui.common.header
import com.android.arttttt.mochamanager.ui.common.levelColor
import com.android.arttttt.mochamanager.ui.common.roll
import com.android.arttttt.mochamanager.ui.common.rollAll
import com.android.arttttt.mochamanager.ui.common.tone
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.subtitle
import com.android.arttttt.mochamanager.ui.main.MainViewModel
import com.android.arttttt.mochamanager.ui.main.Tab
import com.android.arttttt.mochamanager.ui.power.BatteryText
import com.android.arttttt.mochamanager.ui.power.PowerControls
import com.google.android.material.progressindicator.CircularProgressIndicator

class OverviewFragment : Fragment(R.layout.fragment_overview) {
    private val model by screenViewModel { OverviewViewModel(it.power, it.profiles, it.monitor, it.device) }
    private val main: MainViewModel by activityViewModels()
    private var views: Views? = null

    private class Views(root: View, open: (Tab) -> Unit) {
        val units = Units(root.context)
        val ring: CircularProgressIndicator = root.findViewById(R.id.battery_ring)
        val percent: TextView = root.findViewById(R.id.battery_percent)
        val power: TextView = root.findViewById(R.id.power_value)
        val powerSub: TextView = root.findViewById(R.id.power_sub)
        val status: TextView = root.findViewById(R.id.chip_status)
        val temp: TextView = root.findViewById(R.id.chip_temp)
        val wear: TextView = root.findViewById(R.id.chip_wear)
        val cpu = Tile(root, R.id.tile_cpu, R.string.tile_cpu) { open(Tab.PERFORMANCE) }
        val gpu = Tile(root, R.id.tile_gpu, R.string.tile_gpu) { open(Tab.PERFORMANCE) }
        val temperature = Tile(root, R.id.tile_temperature, R.string.tile_temperature) { open(Tab.PERFORMANCE) }
        val memory = Tile(root, R.id.tile_memory, R.string.tile_memory) { open(Tab.PERFORMANCE) }
        private val about: android.view.ViewGroup = root.findViewById(R.id.about_rows)
        val modelName = about.addRow(root.context.getString(R.string.model))
        val memoryVariant = about.addRow(root.context.getString(R.string.memory_variant))
        val build = about.addRow(root.context.getString(R.string.build))
        val kernel = about.addRow(root.context.getString(R.string.kernel))
        val wifi = about.addRow(root.context.getString(R.string.wifi))
        val serial = about.addRow(root.context.getString(R.string.serial))
        lateinit var controls: PowerControls

        init {
            CardTitle(root.findViewById(R.id.about_title)).set(R.string.about)
            cpu.bars.visibility = View.VISIBLE
            gpu.bar.visibility = View.VISIBLE
            memory.bar.visibility = View.VISIBLE
            root.findViewById<View>(R.id.hero).setOnClickListener { open(Tab.POWER) }
        }
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        view.header(R.string.tab_overview)
        views = Views(view) { main.select(it) }.apply {
            controls = PowerControls(view.findViewById(R.id.controls), model::onAction)
        }
        collectWhileShown(model.state, ::render)
        collectWhileShown(model.effects.flow) {
            Toast.makeText(requireContext(), R.string.change_failed, Toast.LENGTH_SHORT).show()
        }
    }

    override fun onDestroyView() {
        views = null
        super.onDestroyView()
    }

    private fun render(state: OverviewState) {
        val v = views ?: return
        val u = v.units
        v.controls.render(state.switches)
        roll(v.power, state.sample, { it.powerMw.toDouble() }) { u.watts(it) }
        rollAll(v.powerSub, state.sample, { doubleArrayOf(it.voltageMv * 1000.0, it.currentMa * 1000.0) }) {
            getString(R.string.overview_power_now, u.volts(it[0]), u.milliamps(it[1]))
        }
        state.battery.valueOrNull?.let { renderBattery(v, it) }
        state.snapshot.valueOrNull?.let { renderSnapshot(v, it) }
        state.device.valueOrNull?.let { renderDevice(v, it) }
    }

    private fun renderBattery(v: Views, b: Battery) {
        val u = v.units
        val level = chargeLevel(b.capacityPct)
        v.ring.setIndicatorColor(v.ring.levelColor(level))
        v.ring.setProgressCompat(b.capacityPct, true)
        v.percent.roll(b.capacityPct.toDouble()) { u.percent(it) }
        v.status.chip(BatteryText.status(requireContext(), b), BatteryText.statusLevel(b))
        v.temp.tone(null)
        v.temp.roll(b.tempDeciC.toDouble()) { u.deciCelsius(it) }
        b.wearPercent?.let { left ->
            v.wear.tone(wearLevel(left))
            v.wear.roll(100 - left) { getString(R.string.wear_chip, u.percent(it)) }
        }
    }

    private fun renderSnapshot(v: Views, s: Snapshot) {
        val u = v.units
        v.cpu.value.roll(s.busiestKhz.toDouble()) { u.mhz(it) }
        /* On LP the one core there is, as the performance screen shows it. */
        val cores = if (s.onLp) s.cores.take(1) else s.cores
        v.cpu.bars.submit(FloatArray(cores.size) { cores[it].load })
        val governor = s.cores.firstOrNull { it.online }?.governor ?: ""
        v.cpu.sub.text = if (s.onLp) getString(R.string.tile_cpu_lp, governor) else governor

        v.gpu.value.roll(s.gpu.curHz.toDouble()) { u.mhzOfHz(it) }
        v.gpu.bar.fill(s.gpu.loadPermille / 1000.0)
        val rail = getString(if (s.gpu.railgated) R.string.gpu_rail_off else R.string.gpu_rail_on)
        v.gpu.sub.roll(s.gpu.loadPermille / 10.0) { getString(R.string.tile_gpu_sub, u.percent(it), rail) }

        val hottest = s.hottest
        if (hottest != null) {
            v.temperature.value.roll(hottest.milliCelsius.toDouble()) { u.celsius(it) }
            v.temperature.value.setTextColor(v.temperature.value.levelColor(thermalLevel(hottest.milliCelsius)))
            v.temperature.sub.text = getString(R.string.tile_temperature_sub, hottest.name)
        }

        val m = s.memory
        v.memory.value.roll(m.availableKb.toDouble()) { u.mb(it) }
        v.memory.bar.fill(m.usedFraction)
        v.memory.sub.roll(doubleArrayOf(m.totalKb.toDouble(), m.swapFraction * 100)) {
            getString(R.string.tile_memory_sub, u.mb(it[0]), u.percent(it[1]))
        }
    }

    private fun renderDevice(v: Views, d: DeviceInfo) {
        val lineage = SystemProperties.get("ro.lineage.build.version")
        view?.subtitle(if (lineage.isEmpty()) d.model else d.model + " · LineageOS " + lineage)
        v.modelName.text = d.model
        v.memoryVariant.text = v.units.gb(d.memoryGb.toDouble()) + " (" + v.units.mb(d.memTotalKb.toDouble()) + ")"
        v.build.text = d.build
        v.kernel.text = d.kernel
        v.wifi.text = d.wifi.ifBlank { getString(R.string.unknown) }
        v.serial.text = d.serial ?: getString(R.string.unknown)
    }
}
