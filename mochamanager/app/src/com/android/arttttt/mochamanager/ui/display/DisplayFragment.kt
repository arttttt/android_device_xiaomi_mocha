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
import android.view.View
import android.widget.RadioButton
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.RefreshMode
import com.android.arttttt.mochamanager.ui.common.Units
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.header
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.text
import com.android.arttttt.mochamanager.ui.common.themeColor
import com.google.android.material.card.MaterialCardView

class DisplayFragment : Fragment(R.layout.fragment_display) {
    private val model by screenViewModel { DisplayViewModel(it.display) }
    private var views: Views? = null

    private class Mode(val card: MaterialCardView, val check: RadioButton)

    private class Views(root: View, choose: (RefreshMode) -> Unit) {
        val units = Units(root.context)
        val rate: TextView = root.findViewById(R.id.rate_value)
        val modes = MODES.map { (mode, card, title, sub) ->
            val view: MaterialCardView = root.findViewById(card)
            view.findViewById<TextView>(R.id.mode_title).setText(title)
            view.findViewById<TextView>(R.id.mode_sub).setText(sub)
            view.setOnClickListener { choose(mode) }
            mode to Mode(view, view.findViewById(R.id.mode_check))
        }.toMap()
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        view.header(R.string.tab_display, getString(R.string.display_subtitle))
        views = Views(view) { model.onAction(DisplayAction.SetMode(it)) }
        collectWhileShown(model.state, ::render)
        collectWhileShown(model.effects.flow) {
            Toast.makeText(requireContext(), R.string.refresh_failed, Toast.LENGTH_SHORT).show()
        }
    }

    override fun onDestroyView() {
        views = null
        super.onDestroyView()
    }

    private fun render(state: DisplayState) {
        val v = views ?: return
        v.rate.text = text(state.refreshRate) { v.units.number("%.2f", it.toDouble()) }
        val chosen = state.mode.valueOrNull
        /* The chosen one outlined in the accent and on its container. */
        for ((mode, m) in v.modes) {
            val on = mode == chosen
            m.check.isChecked = on
            m.card.strokeColor = m.card.themeColor(if (on) R.attr.colorPrimary else R.attr.colorOutline)
            m.card.strokeWidth = resources.displayMetrics.density.let { (if (on) 2 * it else it).toInt() }
            m.card.setCardBackgroundColor(m.card.themeColor(if (on) R.attr.colorPrimaryContainer else R.attr.colorSurface))
        }
    }

    private companion object {
        data class ModeViews(val mode: RefreshMode, val card: Int, val title: Int, val sub: Int)

        val MODES = listOf(
                ModeViews(RefreshMode.AUTO, R.id.mode_auto, R.string.refresh_auto, R.string.refresh_auto_sub),
                ModeViews(RefreshMode.FIXED_60, R.id.mode_60, R.string.refresh_60, R.string.refresh_60_sub),
                ModeViews(RefreshMode.FIXED_30, R.id.mode_30, R.string.refresh_30, R.string.refresh_30_sub))
    }
}
