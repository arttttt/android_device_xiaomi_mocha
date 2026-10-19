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

package com.android.arttttt.mochamanager.di

import android.content.Context
import com.android.arttttt.mochamanager.data.AndroidSensorRepository
import com.android.arttttt.mochamanager.data.DisplaySettingsRepository
import com.android.arttttt.mochamanager.data.ServiceCalibrationRepository
import com.android.arttttt.mochamanager.data.ServiceDeviceRepository
import com.android.arttttt.mochamanager.data.ServiceMonitorRepository
import com.android.arttttt.mochamanager.data.ServicePowerRepository
import com.android.arttttt.mochamanager.data.ServiceProfileRepository
import com.android.arttttt.mochamanager.data.ServiceSource
import com.android.arttttt.mochamanager.data.SystemSource
import com.android.arttttt.mochamanager.domain.repository.CalibrationRepository
import com.android.arttttt.mochamanager.domain.repository.DeviceRepository
import com.android.arttttt.mochamanager.domain.repository.DisplayRepository
import com.android.arttttt.mochamanager.domain.repository.MonitorRepository
import com.android.arttttt.mochamanager.domain.repository.PowerRepository
import com.android.arttttt.mochamanager.domain.repository.ProfileRepository
import com.android.arttttt.mochamanager.domain.repository.SensorRepository
import kotlinx.coroutines.Dispatchers

/*
 * The app's object graph, by hand: a dozen objects need no framework. Each
 * screen's ViewModel takes the repositories it needs from here, and only
 * their interfaces.
 */
class AppContainer(context: Context) {
    private val app = context.applicationContext
    private val service = ServiceSource(Dispatchers.IO)
    private val system = SystemSource(app)

    val device: DeviceRepository by lazy { ServiceDeviceRepository(service, Dispatchers.IO) }
    val power: PowerRepository by lazy { ServicePowerRepository(service) }
    val profiles: ProfileRepository by lazy { ServiceProfileRepository(service, system) }
    val monitor: MonitorRepository by lazy { ServiceMonitorRepository(service) }
    val display: DisplayRepository by lazy { DisplaySettingsRepository(app, system, Dispatchers.IO) }
    val sensors: SensorRepository by lazy { AndroidSensorRepository(app, Dispatchers.Default) }
    val calibration: CalibrationRepository by lazy { ServiceCalibrationRepository(service) }
}
