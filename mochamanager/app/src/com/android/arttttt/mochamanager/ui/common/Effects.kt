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

package com.android.arttttt.mochamanager.ui.common

import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.receiveAsFlow

/*
 * What a screen's model tells it once rather than keeps as state -- a
 * vibration, a message that something failed. Each is taken by one
 * collector, and waits if none is collecting just then.
 */
class Effects<E> {
    private val channel = Channel<E>(Channel.BUFFERED)
    val flow: Flow<E> = channel.receiveAsFlow()

    fun send(effect: E) {
        channel.offer(effect)
    }
}

/* What a screen keeps listening for after it is left: long enough for a
 * rotation, short enough not to poll a screen no one sees. */
const val STOP_TIMEOUT_MS = 5_000L
