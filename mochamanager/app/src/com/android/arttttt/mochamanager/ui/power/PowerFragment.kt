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
import com.android.arttttt.mochamanager.domain.model.PowerMode
import com.android.arttttt.mochamanager.domain.model.Supply
import com.android.arttttt.mochamanager.domain.usecase.wearLevel
import com.android.arttttt.mochamanager.ui.common.Units
import com.android.arttttt.mochamanager.ui.common.addRow
import com.android.arttttt.mochamanager.ui.common.cardTitle
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.fill
import com.android.arttttt.mochamanager.ui.common.header
import com.android.arttttt.mochamanager.ui.common.levelColor
import com.android.arttttt.mochamanager.ui.common.roll
import com.android.arttttt.mochamanager.ui.common.rollAll
import com.android.arttttt.mochamanager.ui.common.settle
import com.android.arttttt.mochamanager.ui.common.tone
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
            controls = PowerControls(view.findViewById(R.id.controls), model::onAction) {
                BatterySheet.ask(this@PowerFragment)
            }
        }
        BatterySheet.onYes(this) { model.onAction(SwitchAction.SetMode(PowerMode.BATTERY)) }
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
        roll(v.value, state.sample, { it.powerMw.toDouble() }) { u.number("%.2f", it / 1000) }
        state.sample.valueOrNull?.let { sample ->
            v.volts.tone(null)
            v.volts.roll(sample.voltageMv * 1000.0) { u.volts(it) }
            v.current.tone(null)
            v.current.roll(sample.currentMa * 1000.0) { u.milliamps(it) }
            sample.energyUWs?.let { e ->
                v.energy.tone(null)
                v.energy.roll(e.toDouble()) { u.wh(it) }
            }
        }
        v.graph.submit(state.history)
        v.controls.render(state.switches)

        val b = state.battery.valueOrNull
        /* On the battery with the cable in, the INA sees all the tablet draws
         * again, as with no cable. */
        val supplied = b != null && b.supply != Supply.NONE && b.powerMode != PowerMode.BATTERY
        v.liveNote.visibility = if (supplied) View.VISIBLE else View.GONE
        roll(v.capacity, state.battery, { it.capacityPct.toDouble() }) { u.percent(it) }
        rollAll(v.charge, state.battery, { doubleArrayOf(it.chargeNowUah.toDouble(), it.chargeFullUah.toDouble()) }) {
            getString(R.string.charge_held, u.mah(it[0]), u.mah(it[1]))
        }
        v.status.text = text(state.battery) { BatteryText.status(requireContext(), it) }
        roll(v.temperature, state.battery, { it.tempDeciC.toDouble() }) { u.deciCelsius(it) }
        v.health.text = text(state.battery) { BatteryText.health(requireContext(), it) }
        roll(v.voltage, state.battery, { it.voltageUv.toDouble() }) { u.volts(it) }
        roll(v.batteryCurrent, state.battery, { it.currentUa.toDouble() }) { u.milliamps(it) }
        if (b != null) renderWear(v, b)
    }

    /* What is left of the design capacity, coloured by how worn it is. */
    private fun renderWear(v: Views, b: Battery) {
        v.health.setTextColor(v.health.levelColor(BatteryText.healthLevel(b)))
        v.wearSub.text = getString(R.string.capacity_left, b.cycleCount)
        val left = b.wearPercent
        if (left == null) {
            v.wear.settle(getString(R.string.unknown))
            return
        }
        val level = wearLevel(left)
        v.wear.roll(left) { v.units.percent(it) }
        v.wear.setTextColor(v.wear.levelColor(level))
        v.wearBar.fill(left / 100, level)
    }
}
