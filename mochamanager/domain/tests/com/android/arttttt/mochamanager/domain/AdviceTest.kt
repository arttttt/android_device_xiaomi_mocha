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

import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import com.android.arttttt.mochamanager.domain.model.Accuracy
import com.android.arttttt.mochamanager.domain.usecase.CalibrationAdvice
import com.android.arttttt.mochamanager.domain.usecase.adviseCalibration
import org.junit.Assert.assertEquals
import org.junit.Test

class AdviceTest {
    private val made = AccelCalibration(true, listOf(0, 0, 508), listOf(1f, 1f, 0.988f), 1)
    private val none = AccelCalibration(false, listOf(0, 0, 0), listOf(1f, 1f, 1f), 0)

    @Test
    fun measuredAccuracyDecides() {
        assertEquals(CalibrationAdvice.NOT_NEEDED, adviseCalibration(Accuracy.HIGH, none))
        assertEquals(CalibrationAdvice.UNKNOWN, adviseCalibration(Accuracy.UNKNOWN, made))
        assertEquals(CalibrationAdvice.CALIBRATE, adviseCalibration(Accuracy.LOW, none))
        assertEquals(CalibrationAdvice.CALIBRATE, adviseCalibration(Accuracy.MEDIUM, null))
        assertEquals(CalibrationAdvice.COULD_IMPROVE, adviseCalibration(Accuracy.MEDIUM, made))
        assertEquals(CalibrationAdvice.RECALIBRATE, adviseCalibration(Accuracy.LOW, made))
    }
}
