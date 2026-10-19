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

import android.os.PowerManager
import android.widget.LinearLayout
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.switchmaterial.SwitchMaterial
import vendor.arttttt.mochamanager.IMochaManager

/* The battery, what it gives, and the two switches the tiles have. */
class PowerFragment : Screen(1000) {
    private lateinit var graph: PowerGraphView
    private lateinit var charging: SwitchMaterial
    private lateinit var profiles: MaterialButtonToggleGroup
    private val buttons = HashMap<Int, Int>()
    /* Set while the views are being brought in line with the service, so
     * their listeners do not take it for the user. */
    private var updating = false

    override fun build() {
        rows.section(R.string.section_live)
        rows.row("power", R.string.power)
        rows.row("ina_voltage", R.string.voltage)
        rows.row("ina_current", R.string.current)
        rows.row("energy", R.string.energy)
        graph = PowerGraphView(requireContext())
        rows.add(graph.apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, rows.dp(140))
        })
        rows.note(R.string.live_note)

        rows.section(R.string.section_switches)
        charging = SwitchMaterial(requireContext()).apply {
            setText(R.string.charging)
            setOnCheckedChangeListener { _, checked ->
                if (!updating) Service.load({ it.setChargingDisabled(!checked) }) { refresh() }
            }
        }
        rows.add(charging)

        profiles = MaterialButtonToggleGroup(requireContext()).apply {
            isSingleSelection = true
            isSelectionRequired = true
        }
        for ((profile, name) in listOf(
                IMochaManager.PROFILE_POWER_SAVE to R.string.profile_power_save,
                IMochaManager.PROFILE_EFFICIENCY to R.string.profile_efficiency,
                IMochaManager.PROFILE_BALANCED to R.string.profile_balanced,
                IMochaManager.PROFILE_PERFORMANCE to R.string.profile_performance)) {
            val button = MaterialButton(requireContext(), null,
                    com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                id = android.view.View.generateViewId()
                setText(name)
                isAllCaps = false
            }
            buttons[button.id] = profile
            profiles.addView(button)
        }
        profiles.addOnButtonCheckedListener { _, id, checked ->
            val profile = buttons[id]
            if (!updating && checked && profile != null) {
                Service.load({ it.setPerformanceProfile(profile) }) { refresh() }
            }
        }
        rows.row("profile_label", R.string.profile)
        rows.add(profiles)
        rows.note(R.string.profile_saver_note)

        rows.section(R.string.section_battery)
        rows.row("capacity", R.string.capacity)
        rows.row("status", R.string.status)
        rows.row("health", R.string.health)
        rows.row("voltage", R.string.voltage)
        rows.row("current", R.string.current)
        rows.row("temp", R.string.temperature)
        rows.row("charge", R.string.charge)
        rows.row("wear", R.string.wear)
        rows.row("cycles", R.string.cycles)
        rows.row("supply", R.string.supply)
    }

    override fun refresh() {
        Service.load({ it.readPower() }) { p ->
            if (p == null) return@load
            rows["power"] = Format.mw(p.powerMw)
            rows["ina_voltage"] = Format.volts(p.voltageMv * 1000)
            rows["ina_current"] = Format.milliamps(p.currentMa * 1000)
            rows["energy"] = if (p.energyUWs >= 0) Format.wh(p.energyUWs) else getString(R.string.unknown)
            graph.add(p.powerMw)
        }
        Service.load({ Pair(it.battery, it.performanceProfile) }) { r ->
            if (r == null) return@load unavailable()
            val (b, profile) = r
            rows["capacity"] = "${b.capacityPct} %"
            rows["status"] = b.status
            rows["health"] = b.health
            rows["voltage"] = Format.volts(b.voltageUv)
            rows["current"] = Format.milliamps(b.currentUa)
            rows["temp"] = Format.deciCelsius(b.tempDeciC)
            rows["charge"] = Format.mah(b.chargeNowUah) + " / " + Format.mah(b.chargeFullUah)
            rows["wear"] = if (b.chargeFullDesignUah > 0) {
                Format.percent(100.0 * b.chargeFullUah / b.chargeFullDesignUah) +
                        " (" + Format.mah(b.chargeFullUah) + " / " + Format.mah(b.chargeFullDesignUah) + ")"
            } else getString(R.string.unknown)
            rows["cycles"] = b.cycleCount.toString()
            rows["supply"] = getString(when {
                b.acOnline -> R.string.supply_ac
                b.usbOnline -> R.string.supply_usb
                else -> R.string.supply_none
            })

            updating = true
            charging.isChecked = !b.chargingDisabled
            val saver = requireContext().getSystemService(PowerManager::class.java).isPowerSaveMode
            for ((id, p) in buttons) if (p == profile) profiles.check(id)
            profiles.isEnabled = !saver
            for (i in 0 until profiles.childCount) profiles.getChildAt(i).isEnabled = !saver
            updating = false
        }
    }
}
