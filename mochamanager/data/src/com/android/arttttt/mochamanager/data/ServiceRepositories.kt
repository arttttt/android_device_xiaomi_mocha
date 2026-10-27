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

package com.android.arttttt.mochamanager.data

import android.os.Build
import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import com.android.arttttt.mochamanager.domain.model.PerformanceProfile
import com.android.arttttt.mochamanager.domain.model.PowerMode
import com.android.arttttt.mochamanager.domain.repository.CalibrationRepository
import com.android.arttttt.mochamanager.domain.repository.DeviceRepository
import com.android.arttttt.mochamanager.domain.repository.MonitorRepository
import com.android.arttttt.mochamanager.domain.repository.PowerRepository
import com.android.arttttt.mochamanager.domain.repository.ProfileRepository
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

/* What the mochamanager service tells and does. */

class ServiceDeviceRepository(
    private val service: ServiceSource,
    private val io: CoroutineDispatcher,
) : DeviceRepository {
    override suspend fun deviceInfo() = service.call { it.deviceInfo }?.toDomain(serial())

    /* READ_PRIVILEGED_PHONE_STATE, which the app is granted as a priv-app. */
    private suspend fun serial(): String? = withContext(io) {
        try {
            Build.getSerial()
        } catch (e: SecurityException) {
            null
        }
    }
}

class ServicePowerRepository(private val service: ServiceSource) : PowerRepository {
    override suspend fun sample() = service.call { it.readPower() }?.toDomain()
    override suspend fun battery() = service.call { it.battery }?.toDomain()
    override suspend fun setPowerMode(mode: PowerMode) =
            service.call { it.setPowerMode(mode.code) } != null
}

class ServiceMonitorRepository(private val service: ServiceSource) : MonitorRepository {
    override suspend fun snapshot() = service.call { it.readSnapshot() }?.toDomain()
}

class ServiceCalibrationRepository(private val service: ServiceSource) : CalibrationRepository {
    override suspend fun calibration() = service.call { it.accelCalibration }?.toDomain()
    override suspend fun save(calibration: AccelCalibration) =
            service.call { it.setAccelCalibration(calibration.toAidl()) } != null
    override suspend fun reset() = service.call { it.resetAccelCalibration() } != null
}

/* The profile is the service's to keep; the battery saver is the system's. */
class ServiceProfileRepository(
    private val service: ServiceSource,
    private val system: SystemSource,
) : ProfileRepository {
    override suspend fun profile() =
            service.call { it.performanceProfile }?.let { PerformanceProfile.of(it) }

    override suspend fun setProfile(profile: PerformanceProfile) =
            service.call { it.setPerformanceProfile(profile.code) } != null

    override val batterySaver = system.batterySaver
}
