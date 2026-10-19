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

package com.android.arttttt.mochamanager.domain.model

/*
 * The accelerometer's calibration, as the sensors HAL takes it: an offset the
 * sensor hub subtracts, in its counts and Android's axes, and a scale per axis
 * the HAL applies.
 */
data class AccelCalibration(
    val present: Boolean,
    val offset: List<Int>,
    val scale: List<Float>,
    /* ms since the epoch; 0 when there is none. */
    val madeMs: Long,
) {
    companion object {
        fun made(offset: List<Int>, scale: List<Float>) = AccelCalibration(true, offset, scale, 0)
    }
}
