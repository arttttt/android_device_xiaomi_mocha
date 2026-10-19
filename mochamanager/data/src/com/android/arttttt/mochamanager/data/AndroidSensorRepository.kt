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

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import com.android.arttttt.mochamanager.domain.model.Accuracy
import com.android.arttttt.mochamanager.domain.model.ReportingMode
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.model.SensorReading
import com.android.arttttt.mochamanager.domain.model.SensorUpdate
import com.android.arttttt.mochamanager.domain.repository.SensorRepository
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.withContext

/*
 * The sensors as SensorManager lists them. SensorManager delivers events
 * through a Looper only, and a coroutine dispatcher has none: they come on
 * the main thread, where the listener does nothing but hand each to its
 * flow. Gathering them is the collector's, on whatever dispatcher it runs
 * on.
 */
class AndroidSensorRepository(
    private val context: Context,
    private val default: CoroutineDispatcher,
) : SensorRepository {
    private val manager = context.getSystemService(SensorManager::class.java)
    private val byKey = HashMap<Int, Sensor>()

    override suspend fun sensors(): List<SensorInfo> = withContext(default) {
        val primary = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        manager.getSensorList(Sensor.TYPE_ALL).mapIndexed { key, sensor ->
            synchronized(byKey) { byKey[key] = sensor }
            SensorInfo(
                    key = key,
                    name = sensor.name,
                    vendor = sensor.vendor,
                    type = sensor.type,
                    reportingMode = when (sensor.reportingMode) {
                        Sensor.REPORTING_MODE_CONTINUOUS -> ReportingMode.CONTINUOUS
                        Sensor.REPORTING_MODE_ON_CHANGE -> ReportingMode.ON_CHANGE
                        Sensor.REPORTING_MODE_ONE_SHOT -> ReportingMode.ONE_SHOT
                        else -> ReportingMode.SPECIAL
                    },
                    maxRateHz = if (sensor.minDelay > 0) 1e6f / sensor.minDelay else null,
                    maxRange = sensor.maximumRange,
                    /* Android's own fusion goes by the vendor name AOSP. */
                    computed = sensor.vendor == "AOSP",
                    restricted = sensor.type == Sensor.TYPE_STEP_COUNTER ||
                            sensor.type == Sensor.TYPE_STEP_DETECTOR,
                    primaryAccelerometer = sensor == primary,
            )
        }
    }

    override fun updates(sensors: List<SensorInfo>): Flow<SensorUpdate> = callbackFlow {
        /* Made whole before the first registration and only read after it:
         * the listener runs on the sensors' thread. A one-shot sensor is
         * armed, not listened to; a restricted one only with the permission. */
        val keys: Map<Sensor, Int> = sensors
                .filter { it.reportingMode != ReportingMode.ONE_SHOT }
                .filter { !it.restricted || stepsAllowed() }
                .mapNotNull { info -> synchronized(byKey) { byKey[info.key] }?.let { it to info.key } }
                .toMap()
        val listener = object : SensorEventListener {
            override fun onSensorChanged(event: SensorEvent) {
                val key = keys[event.sensor] ?: return
                offerIfOpen(SensorUpdate(key, SensorReading(event.values.toList(), accuracy(event.accuracy))))
            }

            /* Every event carries its accuracy. */
            override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
        }
        for (sensor in keys.keys) {
            manager.registerListener(listener, sensor, SensorManager.SENSOR_DELAY_UI)
        }
        /* One listener for all of them: unregistering it takes it off each. */
        awaitClose { manager.unregisterListener(listener) }
    }

    override fun uncalibratedAccelerometer(): Flow<FloatArray>? {
        val sensor = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER_UNCALIBRATED) ?: return null
        return callbackFlow {
            val listener = object : SensorEventListener {
                override fun onSensorChanged(event: SensorEvent) {
                    offerIfOpen(event.values.copyOf(3))
                }
                override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
            }
            manager.registerListener(listener, sensor, SensorManager.SENSOR_DELAY_GAME)
            awaitClose { manager.unregisterListener(listener) }
        }
    }

    override val uncalibratedResolution: Float?
        get() = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER_UNCALIBRATED)?.resolution

    private fun stepsAllowed() = context.checkSelfPermission(
            Manifest.permission.ACTIVITY_RECOGNITION) == PackageManager.PERMISSION_GRANTED

    private fun accuracy(status: Int) = when (status) {
        SensorManager.SENSOR_STATUS_ACCURACY_HIGH -> Accuracy.HIGH
        SensorManager.SENSOR_STATUS_ACCURACY_MEDIUM -> Accuracy.MEDIUM
        SensorManager.SENSOR_STATUS_ACCURACY_LOW -> Accuracy.LOW
        SensorManager.SENSOR_STATUS_UNRELIABLE -> Accuracy.UNRELIABLE
        else -> Accuracy.UNKNOWN
    }
}
