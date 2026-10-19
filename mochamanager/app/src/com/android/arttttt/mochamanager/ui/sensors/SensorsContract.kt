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
import com.android.arttttt.mochamanager.domain.model.SensorReading
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession
import com.android.arttttt.mochamanager.domain.usecase.CalibrationAdvice
import com.android.arttttt.mochamanager.domain.usecase.History

data class SensorsState(
    val groups: Loadable<SensorGroups> = Loadable.Loading,
    /* The latest reading of each sensor listened to, by key. */
    val readings: Map<Int, SensorReading> = emptyMap(),
    /* Steps a step detector has reported since the tab was shown, by key. */
    val steps: Map<Int, Int> = emptyMap(),
    /* The accelerometer's X, Y and Z over the last seconds. */
    val motion: List<History> = emptyList(),
    val calibration: CalibrationState = CalibrationState(),
)

data class CalibrationState(
    val available: Boolean = false,
    val current: Loadable<AccelCalibration> = Loadable.Loading,
    /* As the HAL measures it at rest. */
    val accuracy: Accuracy = Accuracy.UNKNOWN,
    val advice: CalibrationAdvice = CalibrationAdvice.UNKNOWN,
    /* Where one stands, and how the last one ended until it is closed; null
     * when there is none to show. */
    val progress: AccelCalibrationSession.Progress? = null,
)

sealed class SensorsAction {
    object StartCalibration : SensorsAction()
    /* Called off while under way, or put away once it has ended. */
    object CloseCalibration : SensorsAction()
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
