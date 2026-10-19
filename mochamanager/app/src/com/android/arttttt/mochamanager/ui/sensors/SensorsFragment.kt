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

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.VibrationEffect
import android.os.VibratorManager
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Accuracy
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession.Step
import com.android.arttttt.mochamanager.domain.usecase.CalibrationAdvice
import com.android.arttttt.mochamanager.ui.common.Rows
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.text
import com.google.android.material.button.MaterialButton
import java.text.DateFormat
import java.util.Date
import kotlinx.coroutines.Dispatchers

class SensorsFragment : Fragment() {
    private val model by screenViewModel {
        SensorsViewModel(it.sensors, it.calibration, Dispatchers.Default)
    }
    private lateinit var rows: Rows
    private lateinit var format: SensorText
    private lateinit var hint: TextView
    private lateinit var calibrate: MaterialButton
    private lateinit var reset: MaterialButton
    private lateinit var buttons: LinearLayout
    /* The sensors whose rows are on screen, once the list has come. */
    private var shown: List<SensorInfo>? = null

    /* The step sensors are behind a runtime permission, asked for the first
     * time the tab is shown. */
    private val askSteps = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) model.onAction(SensorsAction.PermissionGranted)
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View {
        format = SensorText(requireContext())
        rows = Rows(requireContext())
        shown = null

        rows.section(R.string.section_accel_calibration)
        rows.row(CAL_STATE, R.string.cal_state)
        rows.row(CAL_ACCURACY, R.string.cal_accuracy)
        hint = TextView(requireContext()).apply {
            textSize = 14f
            setPadding(0, rows.dp(6), 0, rows.dp(6))
        }
        rows.add(hint)
        calibrate = MaterialButton(requireContext())
        reset = MaterialButton(requireContext(), null,
                com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
            setText(R.string.cal_reset)
            setOnClickListener { model.onAction(SensorsAction.ResetCalibration) }
        }
        buttons = LinearLayout(requireContext()).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(calibrate)
            addView(reset, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT).apply { marginStart = rows.dp(12) })
        }
        rows.add(buttons)
        return rows.root
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        collectWhileShown(model.state, ::render)
        collectWhileShown(model.effects.flow, ::onEffect)
        if (requireContext().checkSelfPermission(Manifest.permission.ACTIVITY_RECOGNITION) !=
                PackageManager.PERMISSION_GRANTED && saved == null) {
            askSteps.launch(Manifest.permission.ACTIVITY_RECOGNITION)
        }
    }

    private fun render(state: SensorsState) {
        renderCalibration(state.calibration)

        val list = state.sensors.valueOrNull ?: return
        if (shown == null) build(list)
        for (s in list) {
            rows["s${s.key}"] = format.reading(s, state.readings[s.key], state.steps[s.key])
            if (s.primaryAccelerometer) rows[MAGNITUDE] = format.magnitude(state.readings[s.key])
        }
    }

    /* The hub's own first, then what Android derives; within each, by type,
     * so the same kind of reading sits together. */
    private fun build(list: List<SensorInfo>) {
        var section: Boolean? = null
        for (s in list.sortedWith(compareBy<SensorInfo>({ it.computed }, { it.type }, { it.name }))) {
            if (section != s.computed) {
                section = s.computed
                rows.section(if (s.computed) R.string.section_sensors_computed else R.string.section_sensors_hub)
            }
            rows.row("s${s.key}", s.name)
            if (s.primaryAccelerometer) rows.row(MAGNITUDE, R.string.sensor_magnitude)
            rows.note(format.describe(s))
        }
        shown = list
    }

    private fun renderCalibration(c: CalibrationState) {
        buttons.visibility = if (c.available) View.VISIBLE else View.GONE
        rows[CAL_STATE] = text(c.current) {
            if (!it.present) getString(R.string.cal_none)
            else getString(R.string.cal_made, DateFormat.getDateTimeInstance(
                    DateFormat.SHORT, DateFormat.SHORT).format(Date(it.madeMs)))
        }
        rows[CAL_ACCURACY] = getString(when (c.accuracy) {
            Accuracy.HIGH -> R.string.cal_accuracy_high
            Accuracy.MEDIUM -> R.string.cal_accuracy_medium
            Accuracy.LOW, Accuracy.UNRELIABLE -> R.string.cal_accuracy_low
            Accuracy.UNKNOWN -> R.string.cal_accuracy_unknown
        })

        val p = c.progress
        hint.text = when (p?.step) {
            Step.PLACE_UP -> getString(R.string.cal_place_up)
            Step.HOLD_UP, Step.HOLD_DOWN -> getString(R.string.cal_hold, (p.held * 100).toInt())
            Step.PLACE_DOWN -> getString(R.string.cal_place_down)
            Step.DONE -> getString(R.string.cal_done)
            Step.FAILED -> getString(R.string.cal_failed)
            null -> getString(when (c.advice) {
                CalibrationAdvice.NOT_NEEDED -> R.string.cal_advice_good
                CalibrationAdvice.UNKNOWN -> R.string.cal_advice_unknown
                CalibrationAdvice.CALIBRATE -> R.string.cal_advice_none
                CalibrationAdvice.COULD_IMPROVE -> R.string.cal_advice_fair
                CalibrationAdvice.RECALIBRATE -> R.string.cal_advice_redo
            })
        }

        val running = p != null
        calibrate.setText(if (running) R.string.cal_cancel else R.string.cal_start)
        calibrate.setOnClickListener {
            model.onAction(if (running) SensorsAction.CancelCalibration else SensorsAction.StartCalibration)
        }
        reset.isEnabled = !running && c.current.valueOrNull?.present == true
    }

    /*
     * The vibrator HAL plays the platform's effects: a click when a pose has
     * been taken, a double click when the calibration is made, a heavy click
     * when it failed.
     */
    private fun onEffect(effect: SensorsEffect) {
        val haptic = when (effect) {
            SensorsEffect.PoseTaken -> VibrationEffect.EFFECT_CLICK
            SensorsEffect.Calibrated -> VibrationEffect.EFFECT_DOUBLE_CLICK
            SensorsEffect.CalibrationFailed -> VibrationEffect.EFFECT_HEAVY_CLICK
            SensorsEffect.ChangeFailed -> {
                Toast.makeText(requireContext(), R.string.change_failed, Toast.LENGTH_SHORT).show()
                return
            }
        }
        requireContext().getSystemService(VibratorManager::class.java)
                ?.defaultVibrator?.vibrate(VibrationEffect.createPredefined(haptic))
    }

    private companion object {
        const val CAL_STATE = "cal_state"
        const val CAL_ACCURACY = "cal_accuracy"
        const val MAGNITUDE = "magnitude"
    }
}
