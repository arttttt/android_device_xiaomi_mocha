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

package com.android.arttttt.mochamanager.ui.sensors

import android.content.DialogInterface
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ImageView
import android.widget.TextView
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession.Progress
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession.Step
import com.android.arttttt.mochamanager.domain.usecase.Level
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.fill
import com.android.arttttt.mochamanager.ui.common.levelColor
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.themeColor
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import com.google.android.material.progressindicator.LinearProgressIndicator

/*
 * The calibration over the sensors screen: which pose, held how long, and
 * how it ended. It shows the sensors screen's model -- the calibration runs
 * there, and outlives a rotation there -- and only draws it.
 */
class CalibrationSheet : BottomSheetDialogFragment() {
    private val model by screenViewModel({ requireParentFragment() }, SensorsViewModel::create)
    private var views: Views? = null

    private class Views(root: View) {
        val steps: List<View> = listOf(root.findViewById(R.id.step_1), root.findViewById(R.id.step_2))
        val pose: ImageView = root.findViewById(R.id.pose)
        val label: TextView = root.findViewById(R.id.step_label)
        val instruction: TextView = root.findViewById(R.id.instruction)
        val hold: LinearProgressIndicator = root.findViewById(R.id.hold)
        val holdText: TextView = root.findViewById(R.id.hold_text)
        val again: Button = root.findViewById(R.id.again)
        val close: Button = root.findViewById(R.id.close)
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View =
            inflater.inflate(R.layout.sheet_calibration, container, false)

    override fun onViewCreated(view: View, saved: Bundle?) {
        /* All of it at once: the poses are no place to drag a half-open
         * sheet about. */
        (dialog as? BottomSheetDialog)?.behavior?.apply {
            state = BottomSheetBehavior.STATE_EXPANDED
            skipCollapsed = true
        }
        views = Views(view).apply {
            close.setOnClickListener { model.onAction(SensorsAction.CloseCalibration) }
            again.setOnClickListener { model.onAction(SensorsAction.StartCalibration) }
        }
        collectWhileShown(model.state) { s -> s.calibration.progress?.let(::render) }
    }

    override fun onDestroyView() {
        views = null
        super.onDestroyView()
    }

    /* Swiped away or backed out of: called off. */
    override fun onCancel(dialog: DialogInterface) {
        super.onCancel(dialog)
        model.onAction(SensorsAction.CloseCalibration)
    }

    private fun render(p: Progress) {
        val v = views ?: return
        val second = p.step == Step.PLACE_DOWN || p.step == Step.HOLD_DOWN || p.step == Step.DONE
        val ended = p.step == Step.DONE || p.step == Step.FAILED
        for ((i, step) in v.steps.withIndex()) {
            val done = i == 0 || second
            step.backgroundTintList = android.content.res.ColorStateList.valueOf(step.themeColor(
                    if (done) R.attr.colorPrimary else R.attr.colorTrack))
        }
        v.pose.setImageResource(if (second) R.drawable.ic_pose_down else R.drawable.ic_pose_up)
        v.label.text = when (p.step) {
            Step.DONE -> getString(R.string.cal_done)
            Step.FAILED -> getString(R.string.cal_failed_title)
            else -> getString(R.string.cal_step, if (second) "2" else "1")
        }
        v.label.setTextColor(when (p.step) {
            Step.DONE -> v.label.levelColor(Level.GOOD)
            Step.FAILED -> v.label.levelColor(Level.BAD)
            else -> v.label.levelColor(null)
        })
        v.instruction.text = getString(when (p.step) {
            Step.PLACE_UP, Step.HOLD_UP -> R.string.cal_place_up
            Step.PLACE_DOWN, Step.HOLD_DOWN -> R.string.cal_place_down
            Step.DONE -> R.string.cal_done_detail
            Step.FAILED -> R.string.cal_failed
        })
        v.hold.visibility = if (ended) View.GONE else View.VISIBLE
        v.hold.fill(p.held.toDouble())
        v.holdText.text = when (p.step) {
            Step.HOLD_UP, Step.HOLD_DOWN -> getString(R.string.cal_hold, (p.held * 100).toInt().toString())
            Step.PLACE_UP, Step.PLACE_DOWN -> getString(R.string.cal_hint_vibration)
            else -> ""
        }
        v.again.visibility = if (p.step == Step.FAILED) View.VISIBLE else View.GONE
        v.close.setText(if (ended) R.string.cal_close else R.string.cal_cancel)
    }
}
