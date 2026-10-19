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

/* How a reading stands against what is usual for it: what a screen colours. */
enum class Level { GOOD, WARN, BAD }

/*
 * A thermal zone of this SoC: below 45 °C is what it idles and browses at,
 * past 65 °C the governors start capping clocks (the trip points of the
 * CPU and GPU zones sit from 70 °C).
 */
fun thermalLevel(milliCelsius: Int): Level = when {
    milliCelsius < 45_000 -> Level.GOOD
    milliCelsius < 65_000 -> Level.WARN
    else -> Level.BAD
}

/* What is left of the battery's design capacity, percent. */
fun wearLevel(remainingPercent: Double): Level = when {
    remainingPercent >= 80 -> Level.GOOD
    remainingPercent >= 60 -> Level.WARN
    else -> Level.BAD
}

/* The battery's charge, percent: low at 15, as the system warns. */
fun chargeLevel(percent: Int): Level = when {
    percent > 30 -> Level.GOOD
    percent > 15 -> Level.WARN
    else -> Level.BAD
}
