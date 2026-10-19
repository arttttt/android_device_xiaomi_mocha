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

enum class ReportingMode { CONTINUOUS, ON_CHANGE, ONE_SHOT, SPECIAL }

/* How far a sensor's readings can be trusted, as Android's accuracy levels. */
enum class Accuracy { UNKNOWN, UNRELIABLE, LOW, MEDIUM, HIGH }

/*
 * A sensor as the system lists it. type is Android's sensor type, which
 * only the screens interpret.
 */
data class SensorInfo(
    /* Stable for as long as the list is. */
    val key: Int,
    val name: String,
    val vendor: String,
    val type: Int,
    val reportingMode: ReportingMode,
    /* null when it reports no faster than it changes. */
    val maxRateHz: Float?,
    val maxRange: Float,
    /* Computed by Android from other sensors rather than read off one. */
    val computed: Boolean,
    /* Behind the physical activity permission. */
    val restricted: Boolean,
    /* The one Android takes for the accelerometer. */
    val primaryAccelerometer: Boolean,
)

data class SensorReading(val values: List<Float>, val accuracy: Accuracy)

/* One sensor's latest reading, for the screen to keep the last of each. */
data class SensorUpdate(val key: Int, val reading: SensorReading)
