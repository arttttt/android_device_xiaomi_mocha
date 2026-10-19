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

import android.provider.Settings
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.Toast

/*
 * The refresh rate. The composer offers 60 and 30 Hz as one seamless group,
 * and SurfaceFlinger drops to 30 after a while with no new frame; these are
 * the framework's own settings for the range it may choose from.
 */
class DisplayFragment : Screen(1000) {
    private lateinit var choice: RadioGroup
    private val options = HashMap<Int, Pair<Float, Float>>()
    private var updating = false

    override fun build() {
        rows.section(R.string.section_refresh)
        rows.row("current", R.string.refresh_current)

        choice = RadioGroup(requireContext())
        /* min, peak; 0 for no lower bound. */
        for ((name, range) in listOf(
                R.string.refresh_auto to Pair(0f, 60f),
                R.string.refresh_60 to Pair(60f, 60f),
                R.string.refresh_30 to Pair(0f, 30f))) {
            val button = RadioButton(requireContext()).apply {
                id = android.view.View.generateViewId()
                setText(name)
            }
            options[button.id] = range
            choice.addView(button)
        }
        choice.setOnCheckedChangeListener { _, id ->
            val range = options[id] ?: return@setOnCheckedChangeListener
            if (!updating) apply(range)
        }
        rows.add(choice)
    }

    override fun refresh() {
        val rate = requireActivity().display?.refreshRate ?: 0f
        rows["current"] = String.format("%.2f Hz", rate)

        val resolver = requireContext().contentResolver
        val min = Settings.System.getFloat(resolver, Settings.System.MIN_REFRESH_RATE, 0f)
        val peak = Settings.System.getFloat(resolver, Settings.System.PEAK_REFRESH_RATE, 60f)
        updating = true
        for ((id, range) in options) {
            if (range.first == min && range.second == peak) choice.check(id)
        }
        updating = false
    }

    private fun apply(range: Pair<Float, Float>) {
        val resolver = requireContext().contentResolver
        try {
            Settings.System.putFloat(resolver, Settings.System.MIN_REFRESH_RATE, range.first)
            Settings.System.putFloat(resolver, Settings.System.PEAK_REFRESH_RATE, range.second)
        } catch (e: RuntimeException) {
            Toast.makeText(requireContext(), R.string.refresh_failed, Toast.LENGTH_SHORT).show()
        }
        refresh()
    }
}
