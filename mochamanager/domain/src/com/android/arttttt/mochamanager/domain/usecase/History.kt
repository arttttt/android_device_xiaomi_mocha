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
 * The last so many samples, oldest first, for a graph to draw; serial counts
 * the samples ever added, so a graph can tell a new one from a redraw of the
 * same history.
 */
class History private constructor(
    val capacity: Int,
    val samples: List<Float>,
    val serial: Long,
) {
    fun plus(sample: Float): History {
        val kept = if (samples.size < capacity) samples + sample
        else samples.subList(1, capacity) + sample
        return History(capacity, kept, serial + 1)
    }

    companion object {
        fun empty(capacity: Int) = History(capacity, emptyList(), 0)
    }
}
