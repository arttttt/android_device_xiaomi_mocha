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
import com.android.arttttt.mochamanager.domain.model.Accuracy

enum class CalibrationAdvice {
    /* It reads true. */
    NOT_NEEDED,
    /* Nothing measured yet: it has not lain still since it started. */
    UNKNOWN,
    /* Never calibrated, and it reads off. */
    CALIBRATE,
    /* Calibrated and close; another one may make it closer. */
    COULD_IMPROVE,
    /* Calibrated, but it has drifted. */
    RECALIBRATE,
}

/*
 * Whether to calibrate goes by the accuracy the HAL measures at rest, not by
 * whether a calibration was ever made: one made long ago may have drifted.
 */
fun adviseCalibration(accuracy: Accuracy, calibration: AccelCalibration?): CalibrationAdvice = when {
    accuracy == Accuracy.HIGH -> CalibrationAdvice.NOT_NEEDED
    accuracy == Accuracy.UNKNOWN -> CalibrationAdvice.UNKNOWN
    calibration?.present != true -> CalibrationAdvice.CALIBRATE
    accuracy == Accuracy.MEDIUM -> CalibrationAdvice.COULD_IMPROVE
    else -> CalibrationAdvice.RECALIBRATE
}
