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
import androidx.activity.result.contract.ActivityResultContracts
import java.util.Locale
import kotlin.math.sqrt

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

    private val listener = object : SensorEventListener {
        override fun onSensorChanged(event: SensorEvent) {
            if (event.sensor.type == Sensor.TYPE_STEP_DETECTOR) {
                steps[event.sensor] = (steps[event.sensor] ?: 0) + 1
            }
            last[event.sensor] = event.values.clone()
        }

        override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
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
    }

    override fun onPause() {
        manager.unregisterListener(listener)
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
        for ((i, s) in sensors.withIndex()) rows["s$i"] = reading(s)
        val a = accelerometer?.let { last[it] }
        rows["magnitude"] = if (a == null) getString(R.string.unknown) else {
            val g = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
            f("%.2f m/s² (%+.1f %%)", g, (g / SensorManager.GRAVITY_EARTH - 1) * 100)
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
}
