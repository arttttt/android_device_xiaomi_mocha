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

import com.android.arttttt.mochamanager.domain.model.LogLine
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/*
 * The last lines of a log as it grows, at most every periodMs and only when
 * there are new ones: a busy log adds hundreds of lines a second, and a
 * screen redrawn for each would do nothing else. At most capacity lines are
 * kept, the oldest going first.
 */
fun Flow<LogLine>.tail(capacity: Int, periodMs: Long): Flow<List<LogLine>> = channelFlow {
    val lines = ArrayDeque<LogLine>()
    val lock = Mutex()
    var changed = false
    launch {
        collect { line ->
            lock.withLock {
                if (lines.size == capacity) lines.removeFirst()
                lines.addLast(line)
                changed = true
            }
        }
    }
    while (true) {
        val snapshot = lock.withLock {
            if (!changed) null else lines.toList().also { changed = false }
        }
        if (snapshot != null) send(snapshot)
        delay(periodMs)
    }
}
