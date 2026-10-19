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

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.math.sqrt

/*
 * The accelerometer calibrated in two poses: lying screen up, then screen
 * down, each held still for a moment.
 *
 * Read off the uncalibrated sensor -- the chip as it reads, whatever
 * calibration is in force -- an axis that turns over reads +g + bias one
 * way and -g + bias the other, so half the sum of the two is its bias and
 * half the difference its g, whatever its error. That gives Z both its
 * offset and its scale. X and Y bear no gravity in either pose: half their
 * sum is their offset, true as far as the surface is level, and their scale
 * stays as it is.
 *
 * The offset goes to the sensor hub in its counts, the uncalibrated
 * sensor's resolution being one count; the scale the HAL applies.
 */
class AccelCalibrator(
    private val manager: SensorManager,
    private val onChange: (Step) -> Unit,
) {
    enum class Step { IDLE, PLACE_UP, HOLD_UP, PLACE_DOWN, HOLD_DOWN, DONE, FAILED }

    class Result(val offset: IntArray, val scale: FloatArray)

    var step = Step.IDLE
        private set
    var result: Result? = null
        private set
    /* 0..1 while a pose is being held. */
    var progress = 0f
        private set

    private val sensor: Sensor? =
            manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER_UNCALIBRATED)
    private val window = ArrayDeque<FloatArray>()
    private val held = ArrayList<FloatArray>()
    private var up: FloatArray? = null

    private val listener = object : SensorEventListener {
        override fun onSensorChanged(event: SensorEvent) = take(event.values.copyOf(3))
        override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
    }

    val available get() = sensor != null

    fun start() {
        val s = sensor ?: return
        result = null
        up = null
        reset()
        manager.registerListener(listener, s, SensorManager.SENSOR_DELAY_GAME)
        move(Step.PLACE_UP)
    }

    fun cancel() {
        manager.unregisterListener(listener)
        move(Step.IDLE)
    }

    private fun reset() {
        window.clear()
        held.clear()
        progress = 0f
    }

    private fun move(next: Step) {
        step = next
        onChange(next)
    }

    private fun take(a: FloatArray) {
        val wantUp = step == Step.PLACE_UP || step == Step.HOLD_UP
        if (step !in listOf(Step.PLACE_UP, Step.HOLD_UP, Step.PLACE_DOWN, Step.HOLD_DOWN)) return

        window.addLast(a)
        if (window.size > STILL_WINDOW) window.removeFirst()

        /* In the pose -- Z bearing nearly all of g, the right way up -- and
         * still: a second of samples within a tremor of one another. */
        val inPose = if (wantUp) a[2] > POSE_G else a[2] < -POSE_G
        val still = window.size == STILL_WINDOW && spread(window) < STILL_SPREAD
        if (!inPose || !still) {
            if (held.isNotEmpty()) {
                held.clear()
                progress = 0f
                move(if (wantUp) Step.PLACE_UP else Step.PLACE_DOWN)
            }
            return
        }

        if (held.isEmpty()) move(if (wantUp) Step.HOLD_UP else Step.HOLD_DOWN)
        held.add(a)
        progress = held.size.toFloat() / HOLD_SAMPLES
        onChange(step)
        if (held.size < HOLD_SAMPLES) return

        val mean = mean(held)
        reset()
        if (wantUp) {
            up = mean
            move(Step.PLACE_DOWN)
        } else {
            manager.unregisterListener(listener)
            finish(up!!, mean)
        }
    }

    private fun finish(up: FloatArray, down: FloatArray) {
        val step = sensor!!.resolution
        val offset = IntArray(3) { ((up[it] + down[it]) / 2 / step).roundToInt() }
        val zG = (up[2] - down[2]) / 2
        val scaleZ = SensorManager.GRAVITY_EARTH / zG
        if (abs(scaleZ - 1) > 0.1f || offset.any { abs(it) > Short.MAX_VALUE }) {
            move(Step.FAILED)
            return
        }
        result = Result(offset, floatArrayOf(1f, 1f, scaleZ))
        move(Step.DONE)
    }

    private companion object {
        /* About a second at the game rate. */
        const val STILL_WINDOW = 50
        const val STILL_SPREAD = 0.05f
        /* Two seconds held. */
        const val HOLD_SAMPLES = 100
        const val POSE_G = 9.0f

        fun length(a: FloatArray) = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])

        fun spread(samples: Collection<FloatArray>): Float {
            val lengths = samples.map(::length)
            return lengths.maxOrNull()!! - lengths.minOrNull()!!
        }

        fun mean(samples: List<FloatArray>) =
                FloatArray(3) { i -> samples.sumOf { it[i].toDouble() }.toFloat() / samples.size }
    }
}
