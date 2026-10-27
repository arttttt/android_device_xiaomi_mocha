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

import android.view.View
import android.widget.TextView
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.PerformanceProfile
import com.android.arttttt.mochamanager.domain.model.PowerMode
import com.google.android.material.button.MaterialButtonToggleGroup

/*
 * The power_controls views: the power mode and the profile segments, set
 * from Switches and reporting what the user changes. Running on the battery
 * is asked about first: askBattery shows the question, and the mode is set
 * once it is answered yes.
 */
class PowerControls(
    root: View,
    onAction: (SwitchAction) -> Unit,
    askBattery: () -> Unit,
) {
    private val modes: MaterialButtonToggleGroup = root.findViewById(R.id.modes)
    private val modeSub: TextView = root.findViewById(R.id.mode_sub)
    private val warning: View = root.findViewById(R.id.mode_warning)
    private val profiles: MaterialButtonToggleGroup = root.findViewById(R.id.profiles)
    private val note: View = root.findViewById(R.id.profile_note)
    private val notice = BatteryNotice(root.context)
    private val modeButtons = mapOf(
            PowerMode.CHARGE to R.id.mode_charge,
            PowerMode.HOLD to R.id.mode_hold,
            PowerMode.BATTERY to R.id.mode_battery)
    private val buttons = mapOf(
            PerformanceProfile.POWER_SAVE to R.id.profile_power_save,
            PerformanceProfile.EFFICIENCY to R.id.profile_efficiency,
            PerformanceProfile.BALANCED to R.id.profile_balanced,
            PerformanceProfile.PERFORMANCE to R.id.profile_performance)

    /* Set while the views are brought in line with the state, so their
     * listeners do not take it for the user. */
    private var rendering = false
    private var last: Switches? = null

    init {
        modes.addOnButtonCheckedListener { _, id, checked ->
            val mode = modeButtons.entries.firstOrNull { it.value == id }?.key
            if (rendering || !checked || mode == null) return@addOnButtonCheckedListener
            if (mode == PowerMode.BATTERY) {
                /* Not until it is answered: the segment goes back to the mode
                 * in force meanwhile. */
                modes.post { last?.let(::render) }
                askBattery()
            } else {
                onAction(SwitchAction.SetMode(mode))
            }
        }
        profiles.addOnButtonCheckedListener { _, id, checked ->
            val profile = buttons.entries.firstOrNull { it.value == id }?.key
            if (!rendering && checked && profile != null) onAction(SwitchAction.SetProfile(profile))
        }
    }

    fun render(s: Switches) {
        rendering = true
        last = s
        s.mode?.let { modes.check(modeButtons.getValue(it)) }
        modes.isEnabled = s.mode != null
        for (id in modeButtons.values) modes.findViewById<View>(id).isEnabled = s.mode != null
        modeSub.setText(when (s.mode) {
            PowerMode.HOLD -> R.string.mode_hold_sub
            PowerMode.BATTERY -> R.string.mode_battery_sub
            else -> R.string.mode_charge_sub
        })
        warning.visibility = if (s.mode == PowerMode.BATTERY) View.VISIBLE else View.GONE
        s.mode?.let { notice.show(it == PowerMode.BATTERY) }

        s.profile?.let { profiles.check(buttons.getValue(it)) }
        val choosable = s.profile != null && !s.batterySaver
        profiles.isEnabled = choosable
        for (id in buttons.values) profiles.findViewById<View>(id).isEnabled = choosable
        note.visibility = if (s.batterySaver) View.VISIBLE else View.GONE
        rendering = false
    }
}
