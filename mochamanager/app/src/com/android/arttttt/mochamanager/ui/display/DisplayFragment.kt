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

package com.android.arttttt.mochamanager.ui.display

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.Toast
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.RefreshMode
import com.android.arttttt.mochamanager.ui.common.Rows
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.text
import java.util.Locale

class DisplayFragment : Fragment() {
    private val model by screenViewModel { DisplayViewModel(it.display) }
    private lateinit var rows: Rows
    private lateinit var choice: RadioGroup
    private val buttons = HashMap<RefreshMode, RadioButton>()
    private var rendering = false

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View {
        rows = Rows(requireContext())
        rows.section(R.string.section_refresh)
        rows.row(CURRENT, R.string.refresh_current)

        choice = RadioGroup(requireContext())
        for ((mode, name) in MODE_NAMES) {
            val button = RadioButton(requireContext()).apply {
                id = View.generateViewId()
                setText(name)
            }
            buttons[mode] = button
            choice.addView(button)
        }
        choice.setOnCheckedChangeListener { _, id ->
            val mode = buttons.entries.firstOrNull { it.value.id == id }?.key
            if (!rendering && mode != null) model.onAction(DisplayAction.SetMode(mode))
        }
        rows.add(choice)
        return rows.root
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        collectWhileShown(model.state, ::render)
        collectWhileShown(model.effects.flow) {
            Toast.makeText(requireContext(), R.string.refresh_failed, Toast.LENGTH_SHORT).show()
        }
    }

    private fun render(state: DisplayState) {
        rows[CURRENT] = text(state.refreshRate) { String.format(Locale.getDefault(), "%.2f Hz", it) }
        rendering = true
        val mode = state.mode.valueOrNull
        if (mode != null) buttons[mode]?.let { choice.check(it.id) } else choice.clearCheck()
        rendering = false
    }

    private companion object {
        const val CURRENT = "current"
        val MODE_NAMES = listOf(
                RefreshMode.AUTO to R.string.refresh_auto,
                RefreshMode.FIXED_60 to R.string.refresh_60,
                RefreshMode.FIXED_30 to R.string.refresh_30)
    }
}
