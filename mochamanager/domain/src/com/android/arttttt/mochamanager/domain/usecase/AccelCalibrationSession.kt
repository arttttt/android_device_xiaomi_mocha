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

package com.android.arttttt.mochamanager.domain.usecase

import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.math.sqrt

/*
 * The accelerometer calibrated in two poses: lying screen up, then screen
 * down, each held still for a moment, fed the uncalibrated accelerometer --
 * the chip as it reads, whatever calibration is in force.
 *
 * An axis that turns over reads +g + bias one way and -g + bias the other,
 * so half the sum of the two is its bias and half the difference its g,
 * whatever its error. That gives Z both its offset and its scale. X and Y
 * bear no gravity in either pose: half their sum is their offset, true as far
 * as the surface is level, and their scale stays as it is.
 *
 * The offset goes to the sensor hub in its counts, resolution m/s² each; the
 * scale to the HAL.
 */
class AccelCalibrationSession(private val resolution: Float) {

    enum class Step { PLACE_UP, HOLD_UP, PLACE_DOWN, HOLD_DOWN, DONE, FAILED }

    data class Progress(
        val step: Step,
        /* 0..1 while a pose is held. */
        val held: Float,
        val result: AccelCalibration?,
    )

    private val window = ArrayDeque<FloatArray>()
    private val samples = ArrayList<FloatArray>()
    private var up: FloatArray? = null

    var progress = Progress(Step.PLACE_UP, 0f, null)
        private set

    /* Takes a sample, m/s² on X, Y and Z, and says where it stands. */
    fun feed(a: FloatArray): Progress {
        val step = progress.step
        if (step == Step.DONE || step == Step.FAILED) return progress
        val wantUp = step == Step.PLACE_UP || step == Step.HOLD_UP

        window.addLast(a.copyOf(3))
        if (window.size > STILL_WINDOW) window.removeFirst()

        /* In the pose -- Z bearing nearly all of g, the right way up -- and
         * still: a second of samples within a tremor of one another. */
        val inPose = if (wantUp) a[2] > POSE_G else a[2] < -POSE_G
        val still = window.size == STILL_WINDOW && spread(window) < STILL_SPREAD
        if (!inPose || !still) {
            samples.clear()
            progress = Progress(if (wantUp) Step.PLACE_UP else Step.PLACE_DOWN, 0f, null)
            return progress
        }

        samples.add(a.copyOf(3))
        if (samples.size < HOLD_SAMPLES) {
            progress = Progress(if (wantUp) Step.HOLD_UP else Step.HOLD_DOWN,
                    samples.size.toFloat() / HOLD_SAMPLES, null)
            return progress
        }

        val pose = mean(samples)
        samples.clear()
        window.clear()
        progress = if (wantUp) {
            up = pose
            Progress(Step.PLACE_DOWN, 0f, null)
        } else {
            finish(up!!, pose)
        }
        return progress
    }

    private fun finish(up: FloatArray, down: FloatArray): Progress {
        val offset = List(3) { ((up[it] + down[it]) / 2 / resolution).roundToInt() }
        val scaleZ = GRAVITY / ((up[2] - down[2]) / 2)
        if (abs(scaleZ - 1) > MAX_SCALE_ERROR || offset.any { abs(it) > Short.MAX_VALUE }) {
            return Progress(Step.FAILED, 0f, null)
        }
        return Progress(Step.DONE, 1f, AccelCalibration.made(offset, listOf(1f, 1f, scaleZ)))
    }

    companion object {
        const val GRAVITY = 9.80665f
        /* About a second at the game rate. */
        const val STILL_WINDOW = 50
        const val STILL_SPREAD = 0.05f
        /* Two seconds held. */
        const val HOLD_SAMPLES = 100
        const val POSE_G = 9.0f
        /* What the HAL takes: more than a tenth off one is no calibration. */
        const val MAX_SCALE_ERROR = 0.1f

        private fun length(a: FloatArray) = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])

        private fun spread(samples: Collection<FloatArray>): Float {
            val lengths = samples.map(::length)
            return lengths.maxOrNull()!! - lengths.minOrNull()!!
        }

        private fun mean(samples: List<FloatArray>) =
                FloatArray(3) { i -> samples.sumOf { it[i].toDouble() }.toFloat() / samples.size }
    }
}
