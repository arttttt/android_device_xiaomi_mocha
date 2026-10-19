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
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.switchmaterial.SwitchMaterial

/*
 * The power_controls views: the charging switch and the profile segments,
 * set from Switches and reporting what the user changes.
 */
class PowerControls(
    root: View,
    onAction: (SwitchAction) -> Unit,
) {
    private val charging: SwitchMaterial = root.findViewById(R.id.charging)
    private val chargingSub: TextView = root.findViewById(R.id.charging_sub)
    private val profiles: MaterialButtonToggleGroup = root.findViewById(R.id.profiles)
    private val note: View = root.findViewById(R.id.profile_note)
    private val buttons = mapOf(
            PerformanceProfile.POWER_SAVE to R.id.profile_power_save,
            PerformanceProfile.EFFICIENCY to R.id.profile_efficiency,
            PerformanceProfile.BALANCED to R.id.profile_balanced,
            PerformanceProfile.PERFORMANCE to R.id.profile_performance)

    /* Set while the views are brought in line with the state, so their
     * listeners do not take it for the user. */
    private var rendering = false

    init {
        root.findViewById<View>(R.id.charging_row).setOnClickListener {
            if (charging.isEnabled) charging.toggle()
        }
        charging.setOnCheckedChangeListener { _, checked ->
            if (!rendering) onAction(SwitchAction.SetCharging(checked))
        }
        profiles.addOnButtonCheckedListener { _, id, checked ->
            val profile = buttons.entries.firstOrNull { it.value == id }?.key
            if (!rendering && checked && profile != null) onAction(SwitchAction.SetProfile(profile))
        }
    }

    fun render(s: Switches) {
        rendering = true
        s.charging?.let { charging.isChecked = it }
        charging.isEnabled = s.charging != null
        chargingSub.setText(if (s.charging == false) R.string.charging_off else R.string.charging_on)
        s.profile?.let { profiles.check(buttons.getValue(it)) }
        val choosable = s.profile != null && !s.batterySaver
        profiles.isEnabled = choosable
        for (id in buttons.values) profiles.findViewById<View>(id).isEnabled = choosable
        note.visibility = if (s.batterySaver) View.VISIBLE else View.GONE
        rendering = false
    }
}
