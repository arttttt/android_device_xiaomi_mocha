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

package com.android.arttttt.mochamanager

import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import com.android.arttttt.mochamanager.client.MochaManager
import vendor.arttttt.mochamanager.IMochaManager

/*
 * The mochamanager service off the main thread: a call reads sysfs on the
 * other side, which is quick but not something to make the UI wait for. The
 * result comes back on the main thread; null if the service did not answer.
 */
object Service {
    private val worker = HandlerThread("mochamanager").apply { start() }.looper
    private val workerHandler = Handler(worker)
    private val main = Handler(Looper.getMainLooper())

    fun <T> load(block: (IMochaManager) -> T, done: (T?) -> Unit) {
        workerHandler.post {
            val result = MochaManager.call(block)
            main.post { done(result) }
        }
    }
}
