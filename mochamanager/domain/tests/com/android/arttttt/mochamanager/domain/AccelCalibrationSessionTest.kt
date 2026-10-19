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

package com.android.arttttt.mochamanager.domain

import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession.Step
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test

class AccelCalibrationSessionTest {
    private val resolution = 0.000598f

    private fun AccelCalibrationSession.hold(x: Float, y: Float, z: Float, count: Int) =
            (1..count).map { feed(floatArrayOf(x, y, z)) }.last()

    @Test
    fun twoPosesGiveTheOffsetAndTheScale() {
        /* A chip that reads Z 1.2 % high with a bias of 0.3 m/s², X 0.1 off. */
        val gain = 1.012f
        val bias = 0.3f
        val session = AccelCalibrationSession(resolution)

        val afterUp = session.hold(0.1f, 0f, gain * 9.80665f + bias, 200)
        assertEquals(Step.PLACE_DOWN, afterUp.step)

        val done = session.hold(0.1f, 0f, -gain * 9.80665f + bias, 200)
        assertEquals(Step.DONE, done.step)
        val cal = done.result
        assertNotNull(cal)
        cal!!
        assertEquals((0.1f / resolution).toInt().toFloat(), cal.offset[0].toFloat(), 1f)
        assertEquals(0, cal.offset[1])
        assertEquals(bias / resolution, cal.offset[2].toFloat(), 1f)
        assertEquals(1 / gain, cal.scale[2], 1e-4f)
        assertEquals(1f, cal.scale[0], 0f)
    }

    @Test
    fun movingStartsThePoseOver() {
        val session = AccelCalibrationSession(resolution)
        session.hold(0f, 0f, 9.8f, 80)
        assertEquals(Step.HOLD_UP, session.progress.step)
        /* A jolt: out of the stillness window. */
        session.feed(floatArrayOf(0f, 0f, 11f))
        assertEquals(Step.PLACE_UP, session.progress.step)
        assertEquals(0f, session.progress.held, 0f)
    }

    @Test
    fun theWrongWayUpIsNoPose() {
        val session = AccelCalibrationSession(resolution)
        val p = session.hold(0f, 0f, -9.8f, 200)
        assertEquals(Step.PLACE_UP, p.step)
    }

    @Test
    fun aScaleFarOffFails() {
        /* Z swinging 11 m/s² either side of its bias: more than a tenth off
         * g, so no calibration of this chip. */
        val session = AccelCalibrationSession(resolution)
        session.hold(0f, 0f, 13.0f, 200)
        val p = session.hold(0f, 0f, -9.2f, 200)
        assertEquals(Step.FAILED, p.step)
    }
}
