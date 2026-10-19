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

package com.android.arttttt.mochamanager.ui.power

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.Supply
import com.android.arttttt.mochamanager.domain.usecase.wearLevel
import com.android.arttttt.mochamanager.ui.common.Units
import com.android.arttttt.mochamanager.ui.common.addRow
import com.android.arttttt.mochamanager.ui.common.cardTitle
import com.android.arttttt.mochamanager.ui.common.chip
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.fill
import com.android.arttttt.mochamanager.ui.common.header
import com.android.arttttt.mochamanager.ui.common.levelColor
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.text
import com.google.android.material.progressindicator.LinearProgressIndicator

class PowerFragment : Fragment(R.layout.fragment_power) {
    private val model by screenViewModel { PowerViewModel(it.power, it.profiles) }
    private var views: Views? = null

    private class Views(root: View, val units: Units) {
        val value: TextView = root.findViewById(R.id.power_value)
        val volts: TextView = root.findViewById(R.id.chip_volts)
        val current: TextView = root.findViewById(R.id.chip_current)
        val energy: TextView = root.findViewById(R.id.chip_energy)
        val graph: PowerGraphView = root.findViewById(R.id.graph)
        val liveNote: View = root.findViewById(R.id.live_note)
        val capacity: TextView = root.findViewById(R.id.capacity)
        val charge: TextView = root.findViewById(R.id.charge)
        val wear: TextView = root.findViewById(R.id.wear)
        val wearSub: TextView = root.findViewById(R.id.wear_sub)
        val wearBar: LinearProgressIndicator = root.findViewById(R.id.wear_bar)
        private val rows: ViewGroup = root.findViewById(R.id.battery_rows)
        val status = rows.addRow(root.context.getString(R.string.status))
        val temperature = rows.addRow(root.context.getString(R.string.temperature))
        val health = rows.addRow(root.context.getString(R.string.health))
        val voltage = rows.addRow(root.context.getString(R.string.battery_voltage))
        val batteryCurrent = rows.addRow(root.context.getString(R.string.battery_current))
        lateinit var controls: PowerControls
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        view.header(R.string.tab_power, getString(R.string.power_subtitle))
        view.cardTitle(R.id.battery_title, R.string.section_battery)
        view.cardTitle(R.id.controls_title, R.string.section_controls)
        views = Views(view, Units(requireContext())).apply {
            controls = PowerControls(view.findViewById(R.id.controls),
                    model::onAction)
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

    private fun render(state: PowerState) {
        val v = views ?: return
        val u = v.units
        v.value.text = text(state.sample) { u.number("%.2f", it.powerMw / 1000.0) }
        val sample = state.sample.valueOrNull
        if (sample != null) {
            v.volts.chip(u.volts(sample.voltageMv * 1000), null)
            v.current.chip(u.milliamps(sample.currentMa * 1000), null)
            sample.energyUWs?.let { v.energy.chip(u.wh(it), null) }
        }
        v.graph.submit(state.history)
        v.controls.render(state.switches)

        val b = state.battery.valueOrNull
        v.liveNote.visibility = if (b != null && b.supply != Supply.NONE) View.VISIBLE else View.GONE
        v.capacity.text = text(state.battery) { u.percent(it.capacityPct.toDouble()) }
        v.charge.text = text(state.battery) {
            getString(R.string.charge_held, u.mah(it.chargeNowUah), u.mah(it.chargeFullUah))
        }
        v.status.text = text(state.battery) { BatteryText.status(requireContext(), it) }
        v.temperature.text = text(state.battery) { u.deciCelsius(it.tempDeciC) }
        v.health.text = text(state.battery) { BatteryText.health(requireContext(), it) }
        v.voltage.text = text(state.battery) { u.volts(it.voltageUv) }
        v.batteryCurrent.text = text(state.battery) { u.milliamps(it.currentUa) }
        if (b != null) renderWear(v, b)
    }

    /* What is left of the design capacity, coloured by how worn it is. */
    private fun renderWear(v: Views, b: Battery) {
        v.health.setTextColor(v.health.levelColor(BatteryText.healthLevel(b)))
        v.wearSub.text = getString(R.string.capacity_left, b.cycleCount)
        val left = b.wearPercent
        if (left == null) {
            v.wear.setText(R.string.unknown)
            return
        }
        val level = wearLevel(left)
        v.wear.text = v.units.percent(left)
        v.wear.setTextColor(v.wear.levelColor(level))
        v.wearBar.fill(left / 100, level)
    }
}
