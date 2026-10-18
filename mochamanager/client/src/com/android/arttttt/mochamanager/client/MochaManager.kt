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

package com.android.arttttt.mochamanager.client

import android.os.RemoteException
import android.os.ServiceManager
import android.util.Log
import vendor.arttttt.mochamanager.IMochaManager

/*
 * The mochamanager service, asked for and not waited for: a tile is drawn on
 * SystemUI's request and must not hang it on a service that is not up. A
 * call that fails -- no service, or one that died -- is null, and the next
 * one asks again.
 */
object MochaManager {
    private const val TAG = "MochaManagerClient"
    private val INSTANCE = IMochaManager.DESCRIPTOR + "/default"

    fun <T> call(block: (IMochaManager) -> T): T? {
        val service = IMochaManager.Stub.asInterface(ServiceManager.checkService(INSTANCE))
        if (service == null) {
            Log.w(TAG, "$INSTANCE is not running")
            return null
        }
        return try {
            block(service)
        } catch (e: RemoteException) {
            Log.w(TAG, "$INSTANCE failed", e)
            null
        } catch (e: RuntimeException) {
            /* A service-specific error or a rejected argument. */
            Log.w(TAG, "$INSTANCE refused", e)
            null
        }
    }
}
