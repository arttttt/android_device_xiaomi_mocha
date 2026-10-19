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

/*
 * The last two minutes of power, a sample a second, oldest first; serial
 * counts the samples ever added, so a graph can tell a new one from a redraw
 * of the same history.
 */
class PowerHistory private constructor(val samplesMw: List<Int>, val serial: Long) {

    fun plus(powerMw: Int): PowerHistory {
        val samples = if (samplesMw.size < CAPACITY) samplesMw + powerMw
        else samplesMw.subList(1, CAPACITY) + powerMw
        return PowerHistory(samples, serial + 1)
    }

    companion object {
        const val CAPACITY = 120
        val EMPTY = PowerHistory(emptyList(), 0)
    }
}
