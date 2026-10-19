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
import android.os.Handler
import android.os.HandlerThread
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
 * The sensors as SensorManager lists them. Their events are delivered on a
 * thread of their own: a screenful of them comes at a few hundred a second,
 * which is no work for the main thread to take on.
 */
class AndroidSensorRepository(
    private val context: Context,
    private val default: CoroutineDispatcher,
) : SensorRepository {
    private val manager = context.getSystemService(SensorManager::class.java)
    private val events = Handler(HandlerThread("sensors").apply { start() }.looper)
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
        val keys = HashMap<Sensor, Int>()
        val listener = object : SensorEventListener {
            override fun onSensorChanged(event: SensorEvent) {
                val key = keys[event.sensor] ?: return
                offer(SensorUpdate(key, SensorReading(event.values.toList(), accuracy(event.accuracy))))
            }

            /* Every event carries its accuracy. */
            override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
        }
        for (info in sensors) {
            /* A one-shot sensor is armed, not listened to; a restricted one
             * only with the permission. */
            if (info.reportingMode == ReportingMode.ONE_SHOT) continue
            if (info.restricted && !stepsAllowed()) continue
            val sensor = synchronized(byKey) { byKey[info.key] } ?: continue
            keys[sensor] = info.key
            manager.registerListener(listener, sensor, SensorManager.SENSOR_DELAY_UI, events)
        }
        awaitClose { manager.unregisterListener(listener) }
    }

    override fun uncalibratedAccelerometer(): Flow<FloatArray>? {
        val sensor = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER_UNCALIBRATED) ?: return null
        return callbackFlow {
            val listener = object : SensorEventListener {
                override fun onSensorChanged(event: SensorEvent) {
                    offer(event.values.copyOf(3))
                }
                override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}
            }
            manager.registerListener(listener, sensor, SensorManager.SENSOR_DELAY_GAME, events)
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
