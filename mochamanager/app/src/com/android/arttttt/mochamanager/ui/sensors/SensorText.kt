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

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorManager
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.ReportingMode
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.model.SensorReading
import java.util.Locale
import kotlin.math.sqrt

/* How a sensor and its readings read on screen, by Android's sensor type. */
class SensorText(private val context: Context) {

    fun describe(s: SensorInfo): String {
        val mode = context.getString(when (s.reportingMode) {
            ReportingMode.CONTINUOUS -> R.string.sensor_continuous
            ReportingMode.ON_CHANGE -> R.string.sensor_on_change
            ReportingMode.ONE_SHOT -> R.string.sensor_one_shot_mode
            ReportingMode.SPECIAL -> R.string.sensor_special
        })
        val rate = s.maxRateHz?.let { " · " + f("%.0f Hz", it) } ?: ""
        return s.vendor + " · " + mode + rate + " · ±" + trim(s.maxRange)
    }

    fun reading(s: SensorInfo, r: SensorReading?, steps: Int?): String {
        if (s.reportingMode == ReportingMode.ONE_SHOT) return context.getString(R.string.sensor_one_shot)
        if (s.type == Sensor.TYPE_STEP_DETECTOR) return f("%d", steps ?: 0)
        val v = r?.values ?: return context.getString(R.string.unknown)
        return when (s.type) {
            Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GRAVITY, Sensor.TYPE_LINEAR_ACCELERATION ->
                axes(v, "%.2f", "m/s²")
            Sensor.TYPE_ACCELEROMETER_UNCALIBRATED -> uncalibrated(v, "%.2f", "m/s²")
            Sensor.TYPE_GYROSCOPE -> axes(v, "%.3f", "rad/s")
            Sensor.TYPE_GYROSCOPE_UNCALIBRATED -> uncalibrated(v, "%.3f", "rad/s")
            Sensor.TYPE_MAGNETIC_FIELD -> axes(v, "%.1f", "µT")
            Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED -> uncalibrated(v, "%.1f", "µT")
            Sensor.TYPE_ORIENTATION -> angles(v[0], v[1], v[2])
            Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR,
            Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR -> {
                val m = FloatArray(9)
                val o = FloatArray(3)
                SensorManager.getRotationMatrixFromVector(m, v.toFloatArray())
                SensorManager.getOrientation(m, o)
                angles(deg(o[0]), deg(o[1]), deg(o[2]))
            }
            Sensor.TYPE_LIGHT -> f("%.0f lx", v[0])
            Sensor.TYPE_STEP_COUNTER -> f("%.0f", v[0])
            else -> v.joinToString("  ") { f("%.2f", it) }
        }
    }

    /* Its length against standard gravity: what a calibration has to bring
     * right. */
    fun magnitude(r: SensorReading?): String {
        val a = r?.values ?: return context.getString(R.string.unknown)
        val g = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
        return f("%.2f m/s² (%+.1f %%)", g, (g / SensorManager.GRAVITY_EARTH - 1) * 100)
    }

    private fun axes(v: List<Float>, p: String, unit: String) = f("$p  $p  $p", v[0], v[1], v[2]) + " $unit"

    private fun uncalibrated(v: List<Float>, p: String, unit: String) = axes(v, p, unit) + "\n" +
            context.getString(R.string.sensor_bias) + " " + f("$p  $p  $p", v[3], v[4], v[5])

    private fun angles(azimuth: Float, pitch: Float, roll: Float) = f("%.0f°  %.0f°  %.0f°", azimuth, pitch, roll)

    private fun deg(rad: Float) = Math.toDegrees(rad.toDouble()).toFloat()

    private fun trim(value: Float) =
            if (value == value.toLong().toFloat()) value.toLong().toString() else f("%.1f", value)

    private fun f(pattern: String, vararg args: Any) = String.format(Locale.getDefault(), pattern, *args)
}
