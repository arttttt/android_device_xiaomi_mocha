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

import com.android.arttttt.mochamanager.domain.model.Loadable
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.merge

/*
 * A reading taken every periodMs while collected, and at once whenever
 * refresh emits -- after a change the user made, so it shows without waiting
 * for the next period. A period of 0 reads once.
 */
fun <T> poll(
    periodMs: Long,
    refresh: Flow<Unit> = emptyFlow(),
    read: suspend () -> T?,
): Flow<Loadable<T>> {
    val ticks = flow {
        do {
            emit(Unit)
            if (periodMs > 0) delay(periodMs)
        } while (periodMs > 0)
    }
    return merge(ticks, refresh).map { Loadable.of(read()) }
}
