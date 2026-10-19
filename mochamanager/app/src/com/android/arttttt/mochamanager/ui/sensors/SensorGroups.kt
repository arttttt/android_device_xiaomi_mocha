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

import android.hardware.Sensor
import com.android.arttttt.mochamanager.domain.model.SensorInfo

/*
 * The sensors as the screen sets them out: the few one looks at, each in its
 * place, and the rest -- the uncalibrated ones, what Android computes from
 * the others -- folded away. Where the hub and Android both give one kind,
 * the hub's is the one shown.
 */
data class SensorGroups(
    val accelerometer: SensorInfo?,
    val gyroscope: SensorInfo?,
    val linearAcceleration: SensorInfo?,
    val rotation: SensorInfo?,
    val light: SensorInfo?,
    val magneticField: SensorInfo?,
    val stepCounter: SensorInfo?,
    val others: List<SensorInfo>,
) {
    val featured: List<SensorInfo>
        get() = listOfNotNull(accelerometer, gyroscope, linearAcceleration, rotation, light,
                magneticField, stepCounter)

    companion object {
        fun of(list: List<SensorInfo>): SensorGroups {
            val ordered = list.sortedWith(compareBy<SensorInfo>({ it.computed }, { it.type }, { it.name }))
            fun first(vararg types: Int) =
                    types.asSequence().mapNotNull { t -> ordered.firstOrNull { it.type == t } }.firstOrNull()

            val accelerometer = list.firstOrNull { it.primaryAccelerometer } ?: first(Sensor.TYPE_ACCELEROMETER)
            val groups = SensorGroups(
                    accelerometer = accelerometer,
                    gyroscope = first(Sensor.TYPE_GYROSCOPE),
                    linearAcceleration = first(Sensor.TYPE_LINEAR_ACCELERATION),
                    rotation = first(Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR),
                    light = first(Sensor.TYPE_LIGHT),
                    magneticField = first(Sensor.TYPE_MAGNETIC_FIELD),
                    stepCounter = first(Sensor.TYPE_STEP_COUNTER),
                    others = emptyList())
            val featured = groups.featured.map { it.key }.toSet()
            return groups.copy(others = ordered.filter { it.key !in featured })
        }
    }
}
