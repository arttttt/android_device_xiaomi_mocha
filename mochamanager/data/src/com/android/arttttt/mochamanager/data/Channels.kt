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

import kotlinx.coroutines.channels.SendChannel

/*
 * A callback's value into a callbackFlow, dropped if the flow is gone. A
 * listener can still be called between the collector's cancellation and its
 * unregistering, and in kotlinx.coroutines 1.4 offer() into a channel closed
 * by that cancellation throws rather than returning false -- on the
 * callback's thread, which ends the process.
 */
internal fun <E> SendChannel<E>.offerIfOpen(element: E) {
    try {
        offer(element)
    } catch (e: Throwable) {
        /* Closed: nobody is listening any more. */
    }
}
