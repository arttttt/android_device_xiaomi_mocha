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
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.ReportingMode
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.model.SensorReading
import java.util.Locale
import kotlin.math.sqrt

/*
 * How a sensor and its readings read on screen, by Android's sensor type. A
 * vector's three axes are coloured X, Y, Z as the graphs draw them.
 */
class SensorText(private val context: Context, private val axisColors: IntArray) {

    fun describe(s: SensorInfo): String {
        val mode = context.getString(when (s.reportingMode) {
            ReportingMode.CONTINUOUS -> R.string.sensor_continuous
            ReportingMode.ON_CHANGE -> R.string.sensor_on_change
            ReportingMode.ONE_SHOT -> R.string.sensor_one_shot_mode
            ReportingMode.SPECIAL -> R.string.sensor_special
        })
        val rate = s.maxRateHz?.let { " · " + f("%.0f ", it) + u(R.string.unit_hz) } ?: ""
        return s.vendor + " · " + mode + rate + " · ±" + trim(s.maxRange)
    }

    /*
     * A reading as numbers that may run from one to the next -- angles for a
     * rotation vector -- or null when there are none to show: no reading
     * yet, or a sensor that fires once.
     */
    fun numbers(s: SensorInfo, r: SensorReading?, steps: Int?): DoubleArray? {
        if (s.reportingMode == ReportingMode.ONE_SHOT) return null
        if (s.type == Sensor.TYPE_STEP_DETECTOR) return doubleArrayOf((steps ?: 0).toDouble())
        val v = r?.values ?: return null
        return when (s.type) {
            Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR,
            Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR -> orientation(v).map { it.toDouble() }.toDoubleArray()
            else -> v.map { it.toDouble() }.toDoubleArray()
        }
    }

    /* What a sensor's numbers run round on: angles, the short way. */
    fun wrap(s: SensorInfo): Double = when (s.type) {
        Sensor.TYPE_ORIENTATION, Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR,
        Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR -> 360.0
        else -> 0.0
    }

    /* The step a sensor's numbers show in: as many decimals as show() gives. */
    fun step(s: SensorInfo): Double = when (s.type) {
        Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GRAVITY, Sensor.TYPE_LINEAR_ACCELERATION,
        Sensor.TYPE_ACCELEROMETER_UNCALIBRATED -> 0.01
        Sensor.TYPE_GYROSCOPE, Sensor.TYPE_GYROSCOPE_UNCALIBRATED -> 0.001
        Sensor.TYPE_MAGNETIC_FIELD, Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED -> 0.1
        Sensor.TYPE_ORIENTATION, Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR,
        Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR, Sensor.TYPE_LIGHT, Sensor.TYPE_STEP_COUNTER,
        Sensor.TYPE_STEP_DETECTOR -> 1.0
        else -> 0.01
    }

    /* The text for no numbers: what a one-shot sensor is, or a dash. */
    fun none(s: SensorInfo): String = context.getString(
            if (s.reportingMode == ReportingMode.ONE_SHOT) R.string.sensor_one_shot else R.string.unknown)

    fun show(s: SensorInfo, v: DoubleArray): CharSequence = when (s.type) {
        Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GRAVITY, Sensor.TYPE_LINEAR_ACCELERATION ->
            axes(v, "%.2f", u(R.string.unit_ms2))
        Sensor.TYPE_ACCELEROMETER_UNCALIBRATED -> uncalibrated(v, "%.2f", u(R.string.unit_ms2))
        Sensor.TYPE_GYROSCOPE -> axes(v, "%.3f", u(R.string.unit_rads))
        Sensor.TYPE_GYROSCOPE_UNCALIBRATED -> uncalibrated(v, "%.3f", u(R.string.unit_rads))
        Sensor.TYPE_MAGNETIC_FIELD -> axes(v, "%.1f", u(R.string.unit_microtesla))
        Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED -> uncalibrated(v, "%.1f", u(R.string.unit_microtesla))
        Sensor.TYPE_ORIENTATION, Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR,
        Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR -> f("%.0f°  %.0f°  %.0f°", azimuth(v[0]), signed(v[1]), signed(v[2]))
        Sensor.TYPE_LIGHT -> f("%.0f ", v[0]) + u(R.string.unit_lux)
        Sensor.TYPE_STEP_COUNTER, Sensor.TYPE_STEP_DETECTOR -> f("%.0f", v[0])
        else -> v.joinToString("  ") { f("%.2f", it) }
    }

    /* Azimuth, pitch and roll in degrees, from a rotation vector. */
    fun orientation(rotation: List<Float>): FloatArray {
        val m = FloatArray(9)
        val o = FloatArray(3)
        SensorManager.getRotationMatrixFromVector(m, rotation.toFloatArray())
        SensorManager.getOrientation(m, o)
        return FloatArray(3) { Math.toDegrees(o[it].toDouble()).toFloat() }
    }

    /* X, Y and Z, each in its colour, then the unit. */
    fun axes(v: DoubleArray, pattern: String, unit: String): CharSequence {
        val text = SpannableStringBuilder()
        for (i in 0 until 3) {
            if (i > 0) text.append("   ")
            val start = text.length
            text.append(f(pattern, v[i]))
            text.setSpan(ForegroundColorSpan(axisColors[i]), start, text.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        return text.append("  ").append(unit)
    }

    private fun uncalibrated(v: DoubleArray, p: String, unit: String): CharSequence =
            SpannableStringBuilder(axes(v, p, unit)).append("\n")
                    .append(context.getString(R.string.sensor_bias)).append(" ")
                    .append(f("$p  $p  $p", v[3], v[4], v[5]))


    private fun trim(value: Float) =
            if (value == value.toLong().toFloat()) value.toLong().toString() else f("%.1f", value)

    private fun u(id: Int) = context.getString(id)

    private fun f(pattern: String, vararg args: Any) = String.format(Locale.getDefault(), pattern, *args)

    companion object {
        fun length(v: List<Float>) = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).toDouble()

        /* Angles that ran past their range, back into it. */
        fun azimuth(a: Double) = ((a % 360) + 360) % 360
        fun signed(a: Double) = azimuth(a + 180) - 180
    }
}
