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

import com.android.arttttt.mochamanager.client.MochaManager
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import vendor.arttttt.mochamanager.IMochaManager

/*
 * The mochamanager service, off the caller's thread: a call reads sysfs on
 * the other side, quick but not something to make a frame wait for. null
 * when the service does not answer.
 */
class ServiceSource(private val io: CoroutineDispatcher) {
    suspend fun <T> call(block: (IMochaManager) -> T): T? = withContext(io) { MochaManager.call(block) }
}
