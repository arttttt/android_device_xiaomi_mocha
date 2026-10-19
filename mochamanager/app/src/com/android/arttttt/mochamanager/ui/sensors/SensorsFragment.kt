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
import android.hardware.SensorManager
import android.os.Bundle
import android.os.VibrationEffect
import android.os.VibratorManager
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Accuracy
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.usecase.CalibrationAdvice
import com.android.arttttt.mochamanager.domain.usecase.Level
import com.android.arttttt.mochamanager.ui.common.CardTitle
import com.android.arttttt.mochamanager.ui.common.SparklineView
import com.android.arttttt.mochamanager.ui.common.Tile
import com.android.arttttt.mochamanager.ui.common.Units
import com.android.arttttt.mochamanager.ui.common.cardTitle
import com.android.arttttt.mochamanager.ui.common.chip
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.header
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.text
import com.android.arttttt.mochamanager.ui.common.themeColor
import java.text.DateFormat
import java.util.Date

class SensorsFragment : Fragment(R.layout.fragment_sensors) {
    private val model by screenViewModel(create = SensorsViewModel::create)
    private var views: Views? = null

    /* The step sensors are behind a runtime permission, asked for the first
     * time the tab is shown. */
    private val askSteps = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) model.onAction(SensorsAction.PermissionGranted)
    }

    /* An item_sensor: a sensor's name, its reading, what it is. */
    private class Row(root: View) {
        val name: TextView = root.findViewById(R.id.sensor_name)
        val value: TextView = root.findViewById(R.id.sensor_value)
        val about: TextView = root.findViewById(R.id.sensor_about)
    }

    private class Views(root: View) {
        val units = Units(root.context)
        val axes = intArrayOf(root.themeColor(R.attr.colorAxisX), root.themeColor(R.attr.colorAxisY),
                root.themeColor(R.attr.colorAxisZ))
        val format = SensorText(root.context, axes)
        val calTitle = CardTitle(root.findViewById(R.id.cal_title)).apply { set(R.string.section_accel_calibration) }
        val calNow: TextView = root.findViewById(R.id.cal_now)
        val calState: TextView = root.findViewById(R.id.cal_state)
        val calibrate: Button = root.findViewById(R.id.cal_start)
        val reset: Button = root.findViewById(R.id.cal_reset)
        val calibration: View = root.findViewById(R.id.calibration)
        val accelerometer = Row(root.findViewById(R.id.accelerometer))
        val gyroscope = Row(root.findViewById(R.id.gyroscope))
        val linear = Row(root.findViewById(R.id.linear))
        val spark: SparklineView = root.findViewById(R.id.motion_spark)
        val orientation = Tile(root, R.id.tile_orientation, R.string.group_orientation)
        val light = Tile(root, R.id.tile_light, R.string.group_light)
        val magnetic = Tile(root, R.id.tile_magnetic, R.string.group_magnetic)
        val steps = Tile(root, R.id.tile_steps, R.string.group_steps)
        val othersToggle: View = root.findViewById(R.id.others_toggle)
        val othersTitle: TextView = root.findViewById(R.id.others_title)
        val othersAction: TextView = root.findViewById(R.id.others_action)
        val others: ViewGroup = root.findViewById(R.id.others)
        /* The folded-away sensors' rows, by key, made once the list comes. */
        val otherRows = LinkedHashMap<Int, Row>()

        init {
            root.cardTitle(R.id.motion_title, R.string.group_motion)
            accelerometer.name.setText(R.string.accelerometer)
            gyroscope.name.setText(R.string.gyroscope)
            linear.name.setText(R.string.linear_acceleration)
        }
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        view.header(R.string.tab_sensors, getString(R.string.sensors_subtitle))
        views = Views(view).apply {
            calibrate.setOnClickListener { model.onAction(SensorsAction.StartCalibration) }
            reset.setOnClickListener { model.onAction(SensorsAction.ResetCalibration) }
            othersToggle.setOnClickListener { model.onAction(SensorsAction.ToggleOthers) }
        }
        collectWhileShown(model.state, ::render)
        collectWhileShown(model.effects.flow, ::onEffect)
        if (!stepsAllowed() && saved == null) askSteps.launch(Manifest.permission.ACTIVITY_RECOGNITION)
    }

    override fun onDestroyView() {
        views = null
        super.onDestroyView()
    }

    private fun stepsAllowed() = requireContext().checkSelfPermission(Manifest.permission.ACTIVITY_RECOGNITION) ==
            PackageManager.PERMISSION_GRANTED

    private fun render(state: SensorsState) {
        val v = views ?: return
        renderCalibration(v, state)
        showSheet(state.calibration.progress != null)

        val g = state.groups.valueOrNull ?: return
        val r = state.readings
        val f = v.format
        val u = v.units

        g.accelerometer?.let { s ->
            val reading = r[s.key]
            v.accelerometer.value.text = f.reading(s, reading, null)
            v.accelerometer.about.text = reading?.let {
                val a = SensorText.length(it.values)
                getString(R.string.accel_magnitude, u.ms2(a), u.percentFine(((a / SensorManager.GRAVITY_EARTH) - 1) * 100.0)) +
                        " · " + f.describe(s)
            } ?: f.describe(s)
        }
        v.spark.submitCentred(state.motion, v.axes, MOTION_SPAN)
        renderRow(v, v.gyroscope, g.gyroscope, state)
        renderRow(v, v.linear, g.linearAcceleration, state)

        val rotation = g.rotation?.let { r[it.key] }?.values?.let(f::orientation)
        v.orientation.value.text = rotation?.let { u.angle((it[0] + 360) % 360) } ?: getString(R.string.unknown)
        v.orientation.sub.text = rotation?.let { getString(R.string.orientation_sub, u.angle(it[1]), u.angle(it[2])) } ?: ""

        val lux = g.light?.let { r[it.key] }?.values?.get(0)
        v.light.value.text = lux?.let(u::lux) ?: getString(R.string.unknown)
        v.light.sub.text = g.light?.vendor ?: ""

        val field = g.magneticField?.let { r[it.key] }?.values
        v.magnetic.value.text = field?.let { u.microtesla(SensorText.length(it)) } ?: getString(R.string.unknown)
        v.magnetic.sub.setText(R.string.magnetic_sub)

        val steps = g.stepCounter?.let { r[it.key] }?.values?.get(0)
        v.steps.value.text = steps?.let { it.toLong().toString() } ?: getString(R.string.unknown)
        v.steps.sub.setText(if (stepsAllowed()) R.string.steps_sub else R.string.steps_need_permission)

        renderOthers(v, g.others, state)
    }

    private fun renderRow(v: Views, row: Row, s: SensorInfo?, state: SensorsState) {
        row.value.text = s?.let { v.format.reading(it, state.readings[it.key], state.steps[it.key]) }
                ?: getString(R.string.unknown)
        row.about.text = s?.let(v.format::describe) ?: ""
    }

    private fun renderOthers(v: Views, others: List<SensorInfo>, state: SensorsState) {
        v.othersTitle.text = getString(R.string.others, others.size)
        v.othersAction.setText(if (state.othersShown) R.string.hide else R.string.show)
        v.others.visibility = if (state.othersShown) View.VISIBLE else View.GONE
        if (!state.othersShown) return
        if (v.otherRows.isEmpty()) {
            for (s in others) {
                val item = LayoutInflater.from(v.others.context).inflate(R.layout.item_sensor, v.others, false)
                v.others.addView(item)
                v.otherRows[s.key] = Row(item).apply {
                    name.text = s.name
                    about.text = v.format.describe(s)
                }
            }
        }
        for (s in others) {
            v.otherRows[s.key]?.value?.text = v.format.reading(s, state.readings[s.key], state.steps[s.key])
        }
    }

    private fun renderCalibration(v: Views, state: SensorsState) {
        val c = state.calibration
        v.calibration.visibility = if (c.available) View.VISIBLE else View.GONE
        v.calTitle.aside.chip(getString(when (c.accuracy) {
            Accuracy.HIGH -> R.string.cal_accuracy_high
            Accuracy.MEDIUM -> R.string.cal_accuracy_medium
            Accuracy.LOW, Accuracy.UNRELIABLE -> R.string.cal_accuracy_low
            Accuracy.UNKNOWN -> R.string.cal_accuracy_unknown
        }), when (c.accuracy) {
            Accuracy.HIGH -> Level.GOOD
            Accuracy.MEDIUM -> Level.WARN
            Accuracy.LOW, Accuracy.UNRELIABLE -> Level.BAD
            Accuracy.UNKNOWN -> null
        })

        val accel = state.groups.valueOrNull?.accelerometer?.let { state.readings[it.key] }
        v.calNow.text = accel?.let {
            val a = SensorText.length(it.values)
            getString(R.string.cal_now, v.units.ms2(a),
                    v.units.percentFine(((a / SensorManager.GRAVITY_EARTH) - 1) * 100.0))
        } ?: getString(R.string.loading)

        val made = text(c.current) {
            if (!it.present) getString(R.string.cal_none)
            else getString(R.string.cal_made, DateFormat.getDateTimeInstance(
                    DateFormat.SHORT, DateFormat.SHORT).format(Date(it.madeMs)))
        }
        v.calState.text = getString(when (c.advice) {
            CalibrationAdvice.NOT_NEEDED -> R.string.cal_advice_good
            CalibrationAdvice.UNKNOWN -> R.string.cal_advice_unknown
            CalibrationAdvice.CALIBRATE -> R.string.cal_advice_none
            CalibrationAdvice.COULD_IMPROVE -> R.string.cal_advice_fair
            CalibrationAdvice.RECALIBRATE -> R.string.cal_advice_redo
        }).let { " · " }
        v.reset.visibility = if (c.current.valueOrNull?.present == true) View.VISIBLE else View.GONE
        v.calibrate.isEnabled = c.progress == null
    }

    /* The sheet is up for as long as the model has a calibration to show;
     * after a rotation the fragment manager brings it back by itself. */
    private fun showSheet(shown: Boolean) {
        val sheet = childFragmentManager.findFragmentByTag(SHEET) as DialogFragment?
        if (shown && sheet == null) {
            CalibrationSheet().showNow(childFragmentManager, SHEET)
        } else if (!shown && sheet != null) {
            sheet.dismiss()
        }
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
        const val SHEET = "calibration"
        /* m/s²: at rest the lines lie calm, a nudge shows. */
        const val MOTION_SPAN = 0.5f
    }
}
