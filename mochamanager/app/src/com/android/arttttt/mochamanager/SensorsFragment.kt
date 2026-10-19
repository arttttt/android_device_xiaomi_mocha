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

import android.Manifest
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.VibrationEffect
import android.os.VibratorManager
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import com.google.android.material.button.MaterialButton
import java.text.DateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.sqrt
import vendor.arttttt.mochamanager.AccelCalibration

/*
 * Every sensor the system lists, with what it reads right now: the ones the
 * sensor hub gives, and the ones Android computes from them.
 *
 * The sensors are listened to only while the tab is on screen, at the rate a
 * UI is given; the rows are redrawn a few times a second from the last event
 * of each, not on every event.
 */
class SensorsFragment : Screen(200) {
    private lateinit var manager: SensorManager
    private lateinit var sensors: List<Sensor>
    private var accelerometer: Sensor? = null
    private val last = HashMap<Sensor, FloatArray>()
    private val steps = HashMap<Sensor, Int>()

    /* The accelerometer's accuracy as the HAL measures it, at rest, against
     * gravity; -1 until it has said. */
    private var accelAccuracy = -1
    private var calibration: AccelCalibration? = null
    private lateinit var calibrator: AccelCalibrator
    private var lastStep = AccelCalibrator.Step.IDLE
    private lateinit var hint: TextView
    private lateinit var calibrate: MaterialButton
    private lateinit var reset: MaterialButton

    private val listener = object : SensorEventListener {
        override fun onSensorChanged(event: SensorEvent) {
            if (event.sensor.type == Sensor.TYPE_STEP_DETECTOR) {
                steps[event.sensor] = (steps[event.sensor] ?: 0) + 1
            }
            last[event.sensor] = event.values.clone()
            if (event.sensor == accelerometer) accelAccuracy = event.accuracy
        }

        override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {
            if (sensor == accelerometer) accelAccuracy = accuracy
        }
    }

    /* The step sensors are behind a runtime permission; asked for once, the
     * first time the tab is shown. */
    private val askSteps = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted && isResumed) listen()
    }

    override fun build() {
        manager = requireContext().getSystemService(SensorManager::class.java)
        accelerometer = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        /* The hub's own first, then what Android derives; within each, by
         * type, so the same kind of reading sits together. */
        sensors = manager.getSensorList(Sensor.TYPE_ALL)
                .sortedWith(compareBy<Sensor>({ computed(it) }, { it.type }, { it.name }))

        calibrator = AccelCalibrator(manager) { step ->
            val previous = lastStep
            lastStep = step
            if (step != previous) signal(previous, step)
            refreshCalibration()
        }
        buildCalibration()

        var section: Boolean? = null
        for ((i, s) in sensors.withIndex()) {
            if (section != computed(s)) {
                section = computed(s)
                rows.section(if (section) R.string.section_sensors_computed else R.string.section_sensors_hub)
            }
            rows.row("s$i", s.name)
            if (s == accelerometer) rows.row("magnitude", R.string.sensor_magnitude)
            rows.note(describe(s))
        }

        if (!stepsAllowed()) askSteps.launch(Manifest.permission.ACTIVITY_RECOGNITION)
    }

    override fun onResume() {
        super.onResume()
        listen()
        loadCalibration()
    }

    override fun onPause() {
        manager.unregisterListener(listener)
        if (calibrator.step != AccelCalibrator.Step.IDLE) calibrator.cancel()
        super.onPause()
    }

    private fun listen() {
        manager.unregisterListener(listener)
        for (s in sensors) {
            /* A one-shot sensor is not listened to, it is armed; and it would
             * say only that the tablet has been carried somewhere. */
            if (s.reportingMode == Sensor.REPORTING_MODE_ONE_SHOT) continue
            if (s.stepSensor() && !stepsAllowed()) continue
            manager.registerListener(listener, s, SensorManager.SENSOR_DELAY_UI)
        }
    }

    override fun refresh() {
        refreshCalibration()
        for ((i, s) in sensors.withIndex()) rows["s$i"] = reading(s)
        val a = accelerometer?.let { last[it] }
        rows["magnitude"] = if (a == null) getString(R.string.unknown) else {
            val g = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
            f("%.2f m/s² (%+.1f %%)", g, (g / SensorManager.GRAVITY_EARTH - 1) * 100)
        }
    }

    /* ---- the accelerometer's calibration ---- */

    private fun buildCalibration() {
        rows.section(R.string.section_accel_calibration)
        rows.row("cal_state", R.string.cal_state)
        rows.row("cal_accuracy", R.string.cal_accuracy)

        hint = TextView(requireContext()).apply {
            textSize = 14f
            setPadding(0, rows.dp(6), 0, rows.dp(6))
        }
        rows.add(hint)

        calibrate = MaterialButton(requireContext()).apply {
            setOnClickListener {
                if (calibrator.step in IN_PROGRESS) calibrator.cancel() else calibrator.start()
            }
        }
        reset = MaterialButton(requireContext(), null,
                com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
            setText(R.string.cal_reset)
            setOnClickListener {
                Service.load({ it.resetAccelCalibration() }) { loadCalibration() }
            }
        }
        val buttons = LinearLayout(requireContext()).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(calibrate)
            addView(reset, LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT)
                    .apply { marginStart = rows.dp(12) })
        }
        rows.add(buttons)
        if (!calibrator.available) buttons.visibility = View.GONE
    }

    private fun loadCalibration() {
        Service.load({ it.accelCalibration }) { c ->
            calibration = c
            refreshCalibration()
        }
    }

    private fun refreshCalibration() {
        if (!isAdded) return
        val c = calibration
        rows["cal_state"] = when {
            c == null -> getString(R.string.unknown)
            !c.present -> getString(R.string.cal_none)
            else -> getString(R.string.cal_made,
                    DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
                            .format(Date(c.madeMs)))
        }
        rows["cal_accuracy"] = getString(when (accelAccuracy) {
            SensorManager.SENSOR_STATUS_ACCURACY_HIGH -> R.string.cal_accuracy_high
            SensorManager.SENSOR_STATUS_ACCURACY_MEDIUM -> R.string.cal_accuracy_medium
            SensorManager.SENSOR_STATUS_ACCURACY_LOW -> R.string.cal_accuracy_low
            else -> R.string.cal_accuracy_unknown
        })

        val step = calibrator.step
        hint.text = when (step) {
            AccelCalibrator.Step.PLACE_UP -> getString(R.string.cal_place_up)
            AccelCalibrator.Step.HOLD_UP, AccelCalibrator.Step.HOLD_DOWN ->
                getString(R.string.cal_hold, (calibrator.progress * 100).toInt())
            AccelCalibrator.Step.PLACE_DOWN -> getString(R.string.cal_place_down)
            AccelCalibrator.Step.FAILED -> getString(R.string.cal_failed)
            AccelCalibrator.Step.DONE -> getString(R.string.cal_done)
            AccelCalibrator.Step.IDLE -> getString(advice(c))
        }
        calibrate.setText(if (step in IN_PROGRESS) R.string.cal_cancel else R.string.cal_start)
        reset.isEnabled = step !in IN_PROGRESS && c?.present == true

        if (step == AccelCalibrator.Step.DONE) save()
    }

    /*
     * Felt rather than seen: the screen faces the table for the second pose.
     * One short pulse when a pose has been taken, two when the calibration
     * is made, a long one when it failed.
     */
    private fun signal(from: AccelCalibrator.Step, to: AccelCalibrator.Step) {
        val pattern = when {
            from == AccelCalibrator.Step.HOLD_UP && to == AccelCalibrator.Step.PLACE_DOWN ->
                longArrayOf(0, 60)
            to == AccelCalibrator.Step.DONE -> longArrayOf(0, 60, 120, 60)
            to == AccelCalibrator.Step.FAILED -> longArrayOf(0, 400)
            else -> return
        }
        val vibrator = requireContext().getSystemService(VibratorManager::class.java)
                ?.defaultVibrator ?: return
        vibrator.vibrate(VibrationEffect.createWaveform(pattern, -1))
    }

    /* Whether to calibrate goes by what the HAL measures, not by whether a
     * calibration was ever made: one made long ago may have drifted. */
    private fun advice(c: AccelCalibration?) = when {
        accelAccuracy == SensorManager.SENSOR_STATUS_ACCURACY_HIGH -> R.string.cal_advice_good
        accelAccuracy < 0 -> R.string.cal_advice_unknown
        c?.present != true -> R.string.cal_advice_none
        accelAccuracy == SensorManager.SENSOR_STATUS_ACCURACY_MEDIUM -> R.string.cal_advice_fair
        else -> R.string.cal_advice_redo
    }

    private fun save() {
        val result = calibrator.result ?: return
        calibrator.cancel()
        val made = AccelCalibration().apply {
            present = true
            offset = result.offset
            scale = result.scale
        }
        Service.load({ it.setAccelCalibration(made) }) {
            accelAccuracy = -1
            loadCalibration()
        }
    }

    private fun reading(s: Sensor): String {
        if (s.reportingMode == Sensor.REPORTING_MODE_ONE_SHOT) return getString(R.string.sensor_one_shot)
        if (s.stepSensor() && !stepsAllowed()) return getString(R.string.sensor_no_permission)
        val v = last[s] ?: return getString(R.string.unknown)
        return when (s.type) {
            Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GRAVITY, Sensor.TYPE_LINEAR_ACCELERATION ->
                axes(v, "%.2f", "m/s²")
            Sensor.TYPE_GYROSCOPE -> axes(v, "%.3f", "rad/s")
            Sensor.TYPE_GYROSCOPE_UNCALIBRATED -> axes(v, "%.3f", "rad/s") + "\n" +
                    getString(R.string.sensor_bias) + " " + axes(v.copyOfRange(3, 6), "%.3f", "")
            Sensor.TYPE_MAGNETIC_FIELD -> axes(v, "%.1f", "µT")
            Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED -> axes(v, "%.1f", "µT") + "\n" +
                    getString(R.string.sensor_bias) + " " + axes(v.copyOfRange(3, 6), "%.1f", "")
            Sensor.TYPE_ORIENTATION -> angles(v[0], v[1], v[2])
            Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR,
            Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR -> {
                val m = FloatArray(9)
                val o = FloatArray(3)
                SensorManager.getRotationMatrixFromVector(m, v)
                SensorManager.getOrientation(m, o)
                angles(deg(o[0]), deg(o[1]), deg(o[2]))
            }
            Sensor.TYPE_LIGHT -> f("%.0f lx", v[0])
            Sensor.TYPE_STEP_COUNTER -> f("%.0f", v[0])
            Sensor.TYPE_STEP_DETECTOR -> f("%d", steps[s] ?: 0)
            else -> v.joinToString("  ") { f("%.2f", it) }
        }
    }

    private fun describe(s: Sensor): String {
        val mode = getString(when (s.reportingMode) {
            Sensor.REPORTING_MODE_CONTINUOUS -> R.string.sensor_continuous
            Sensor.REPORTING_MODE_ON_CHANGE -> R.string.sensor_on_change
            Sensor.REPORTING_MODE_ONE_SHOT -> R.string.sensor_one_shot_mode
            else -> R.string.sensor_special
        })
        val rate = if (s.minDelay > 0) " · " + f("%.0f Hz", 1e6 / s.minDelay) else ""
        return s.vendor + " · " + mode + rate + " · " + f("±%s", trim(s.maximumRange))
    }

    private fun computed(s: Sensor) = s.vendor == "AOSP"

    private fun Sensor.stepSensor() = type == Sensor.TYPE_STEP_COUNTER || type == Sensor.TYPE_STEP_DETECTOR

    private fun stepsAllowed() = requireContext().checkSelfPermission(
            Manifest.permission.ACTIVITY_RECOGNITION) == PackageManager.PERMISSION_GRANTED

    private fun axes(v: FloatArray, p: String, unit: String) =
            f("$p  $p  $p", v[0], v[1], v[2]) + if (unit.isEmpty()) "" else " $unit"

    private fun angles(azimuth: Float, pitch: Float, roll: Float) = f("%.0f°  %.0f°  %.0f°", azimuth, pitch, roll)

    private fun deg(rad: Float) = Math.toDegrees(rad.toDouble()).toFloat()

    private fun trim(value: Float) = if (value == value.toLong().toFloat()) value.toLong().toString() else f("%.1f", value)

    private fun f(pattern: String, vararg args: Any) = String.format(Locale.getDefault(), pattern, *args)

    private companion object {
        val IN_PROGRESS = listOf(AccelCalibrator.Step.PLACE_UP, AccelCalibrator.Step.HOLD_UP,
                AccelCalibrator.Step.PLACE_DOWN, AccelCalibrator.Step.HOLD_DOWN)
    }
}
