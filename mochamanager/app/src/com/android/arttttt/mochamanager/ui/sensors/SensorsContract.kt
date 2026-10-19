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

import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import com.android.arttttt.mochamanager.domain.model.Accuracy
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.model.SensorReading
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession
import com.android.arttttt.mochamanager.domain.usecase.CalibrationAdvice

data class SensorsState(
    val sensors: Loadable<List<SensorInfo>> = Loadable.Loading,
    /* The latest reading of each sensor, by key. */
    val readings: Map<Int, SensorReading> = emptyMap(),
    /* Steps a step detector has reported since the tab was shown, by key. */
    val steps: Map<Int, Int> = emptyMap(),
    val calibration: CalibrationState = CalibrationState(),
)

data class CalibrationState(
    val available: Boolean = false,
    val current: Loadable<AccelCalibration> = Loadable.Loading,
    /* As the HAL measures it at rest. */
    val accuracy: Accuracy = Accuracy.UNKNOWN,
    val advice: CalibrationAdvice = CalibrationAdvice.UNKNOWN,
    /* null unless one is under way. */
    val progress: AccelCalibrationSession.Progress? = null,
)

sealed class SensorsAction {
    object StartCalibration : SensorsAction()
    object CancelCalibration : SensorsAction()
    object ResetCalibration : SensorsAction()
    /* The step sensors' permission was granted; listen to them too. */
    object PermissionGranted : SensorsAction()
}

sealed class SensorsEffect {
    /* Felt rather than seen: the screen faces the table for the second pose. */
    object PoseTaken : SensorsEffect()
    object Calibrated : SensorsEffect()
    object CalibrationFailed : SensorsEffect()
    object ChangeFailed : SensorsEffect()
}
