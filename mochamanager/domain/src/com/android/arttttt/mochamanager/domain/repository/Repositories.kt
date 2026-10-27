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

package com.android.arttttt.mochamanager.domain.repository

import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.CrashKind
import com.android.arttttt.mochamanager.domain.model.CrashRecord
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.LogBuffer
import com.android.arttttt.mochamanager.domain.model.LogKind
import com.android.arttttt.mochamanager.domain.model.LogLine
import com.android.arttttt.mochamanager.domain.model.PerformanceProfile
import com.android.arttttt.mochamanager.domain.model.PowerMode
import com.android.arttttt.mochamanager.domain.model.PowerSample
import com.android.arttttt.mochamanager.domain.model.RefreshMode
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.model.SensorUpdate
import com.android.arttttt.mochamanager.domain.model.Snapshot
import kotlinx.coroutines.flow.Flow

/*
 * What the screens need from the tablet, as the domain sees it. A read is a
 * suspending call that is null when the source does not answer; a value
 * that changes by itself is a Flow. The data layer decides where the work
 * runs: none of these may be called for on a thread that cannot wait.
 */

interface DeviceRepository {
    suspend fun deviceInfo(): DeviceInfo?
}

interface PowerRepository {
    suspend fun sample(): PowerSample?
    suspend fun battery(): Battery?
    suspend fun setPowerMode(mode: PowerMode): Boolean
}

interface ProfileRepository {
    suspend fun profile(): PerformanceProfile?
    suspend fun setProfile(profile: PerformanceProfile): Boolean
    /* While it is on, the battery saver holds power save whatever the
     * choice. */
    val batterySaver: Flow<Boolean>
}

interface MonitorRepository {
    suspend fun snapshot(): Snapshot?
}

interface DisplayRepository {
    /* Hz, as the display runs now. */
    val refreshRate: Flow<Float>
    /* null for a range none of the modes is. */
    val mode: Flow<RefreshMode?>
    suspend fun setMode(mode: RefreshMode): Boolean
}

interface SensorRepository {
    suspend fun sensors(): List<SensorInfo>
    /* Every reading of the sensors given, while collected. */
    fun updates(sensors: List<SensorInfo>): Flow<SensorUpdate>
    /* The accelerometer as the chip reads it, m/s²; null if there is none. */
    fun uncalibratedAccelerometer(): Flow<FloatArray>?
    /* m/s² per count of the uncalibrated accelerometer. */
    val uncalibratedResolution: Float?
}

interface CalibrationRepository {
    suspend fun calibration(): AccelCalibration?
    suspend fun save(calibration: AccelCalibration): Boolean
    suspend fun reset(): Boolean
}

interface LogRepository {
    /* The log as it grows while collected: its last lines first, then each
     * new one. buffers only matter for the system log. */
    fun follow(kind: LogKind, buffers: Set<LogBuffer>): Flow<LogLine>
    /* All of the log there is now, as text. */
    suspend fun dump(kind: LogKind, buffers: Set<LogBuffer>): String?
    /* The system's records of these kinds, at most limit of each, oldest
     * first. */
    suspend fun crashes(kinds: Set<CrashKind>, limit: Int): List<CrashRecord>
    /* The properties a report on the tablet names. */
    suspend fun properties(): Map<String, String>
}
