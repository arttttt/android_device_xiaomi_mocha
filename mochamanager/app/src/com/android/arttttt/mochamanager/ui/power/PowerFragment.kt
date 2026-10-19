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
import android.widget.LinearLayout
import android.widget.Toast
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.PerformanceProfile
import com.android.arttttt.mochamanager.domain.model.Supply
import com.android.arttttt.mochamanager.ui.common.Format
import com.android.arttttt.mochamanager.ui.common.Rows
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.text
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.switchmaterial.SwitchMaterial

class PowerFragment : Fragment() {
    private val model by screenViewModel { PowerViewModel(it.power, it.profiles) }
    private lateinit var rows: Rows
    private lateinit var graph: PowerGraphView
    private lateinit var charging: SwitchMaterial
    private lateinit var profiles: MaterialButtonToggleGroup
    private val buttons = HashMap<PerformanceProfile, MaterialButton>()

    /* Set while the views are brought in line with the state, so their
     * listeners do not take it for the user. */
    private var rendering = false

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View {
        rows = Rows(requireContext())
        rows.section(R.string.section_live)
        rows.row(POWER, R.string.power)
        rows.row(INA_VOLTAGE, R.string.voltage)
        rows.row(INA_CURRENT, R.string.current)
        rows.row(ENERGY, R.string.energy)
        graph = PowerGraphView(requireContext())
        rows.add(graph.apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, rows.dp(140))
        })
        rows.note(R.string.live_note)

        rows.section(R.string.section_switches)
        charging = SwitchMaterial(requireContext()).apply {
            setText(R.string.charging)
            setOnCheckedChangeListener { _, checked ->
                if (!rendering) model.onAction(PowerAction.SetCharging(checked))
            }
        }
        rows.add(charging)

        profiles = MaterialButtonToggleGroup(requireContext()).apply {
            isSingleSelection = true
            isSelectionRequired = true
        }
        for ((profile, name) in PROFILE_NAMES) {
            val button = MaterialButton(requireContext(), null,
                    com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                id = View.generateViewId()
                setText(name)
                isAllCaps = false
            }
            buttons[profile] = button
            profiles.addView(button)
        }
        profiles.addOnButtonCheckedListener { _, id, checked ->
            val profile = buttons.entries.firstOrNull { it.value.id == id }?.key
            if (!rendering && checked && profile != null) model.onAction(PowerAction.SetProfile(profile))
        }
        rows.row(PROFILE, R.string.profile)
        rows.add(profiles)
        rows.note(R.string.profile_saver_note)

        rows.section(R.string.section_battery)
        for ((key, label) in BATTERY_ROWS) rows.row(key, label)
        return rows.root
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        collectWhileShown(model.state, ::render)
        collectWhileShown(model.effects.flow) {
            Toast.makeText(requireContext(), R.string.change_failed, Toast.LENGTH_SHORT).show()
        }
    }

    private fun render(state: PowerState) {
        rows[POWER] = text(state.sample) { Format.mw(it.powerMw) }
        rows[INA_VOLTAGE] = text(state.sample) { Format.volts(it.voltageMv * 1000) }
        rows[INA_CURRENT] = text(state.sample) { Format.milliamps(it.currentMa * 1000) }
        rows[ENERGY] = text(state.sample) { s -> s.energyUWs?.let(Format::wh) ?: getString(R.string.unknown) }
        graph.submit(state.history)

        for ((key, _) in BATTERY_ROWS) rows[key] = text(state.battery) { battery(key, it) }

        rendering = true
        state.battery.valueOrNull?.let { charging.isChecked = !it.chargingDisabled }
        charging.isEnabled = state.battery.valueOrNull != null
        state.profile.valueOrNull?.let { buttons[it]?.let { b -> profiles.check(b.id) } }
        val choosable = state.profile.valueOrNull != null && !state.batterySaver
        profiles.isEnabled = choosable
        for (button in buttons.values) button.isEnabled = choosable
        rendering = false
    }

    private fun battery(key: String, b: Battery): CharSequence = when (key) {
        CAPACITY -> "${b.capacityPct} %"
        STATUS -> b.status
        HEALTH -> b.health
        VOLTAGE -> Format.volts(b.voltageUv)
        CURRENT -> Format.milliamps(b.currentUa)
        TEMP -> Format.deciCelsius(b.tempDeciC)
        CHARGE -> Format.mah(b.chargeNowUah) + " / " + Format.mah(b.chargeFullUah)
        WEAR -> b.wearPercent?.let {
            Format.percent(it) + " (" + Format.mah(b.chargeFullUah) + " / " +
                    Format.mah(b.chargeFullDesignUah) + ")"
        } ?: getString(R.string.unknown)
        CYCLES -> b.cycleCount.toString()
        else -> getString(when (b.supply) {
            Supply.AC -> R.string.supply_ac
            Supply.USB -> R.string.supply_usb
            Supply.NONE -> R.string.supply_none
        })
    }

    private companion object {
        const val POWER = "power"
        const val INA_VOLTAGE = "ina_voltage"
        const val INA_CURRENT = "ina_current"
        const val ENERGY = "energy"
        const val PROFILE = "profile"
        const val CAPACITY = "capacity"
        const val STATUS = "status"
        const val HEALTH = "health"
        const val VOLTAGE = "voltage"
        const val CURRENT = "current"
        const val TEMP = "temp"
        const val CHARGE = "charge"
        const val WEAR = "wear"
        const val CYCLES = "cycles"
        const val SUPPLY = "supply"

        val PROFILE_NAMES = listOf(
                PerformanceProfile.POWER_SAVE to R.string.profile_power_save,
                PerformanceProfile.EFFICIENCY to R.string.profile_efficiency,
                PerformanceProfile.BALANCED to R.string.profile_balanced,
                PerformanceProfile.PERFORMANCE to R.string.profile_performance)

        val BATTERY_ROWS = listOf(
                CAPACITY to R.string.capacity,
                STATUS to R.string.status,
                HEALTH to R.string.health,
                VOLTAGE to R.string.voltage,
                CURRENT to R.string.current,
                TEMP to R.string.temperature,
                CHARGE to R.string.charge,
                WEAR to R.string.wear,
                CYCLES to R.string.cycles,
                SUPPLY to R.string.supply)
    }
}
