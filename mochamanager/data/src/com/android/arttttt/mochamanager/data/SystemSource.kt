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

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.view.Display
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.distinctUntilChanged

/*
 * What the system tells by callback, as flows that listen while collected:
 * a broadcast, a setting, the display. The callbacks come on the main
 * thread, where these listeners are cheap.
 */
class SystemSource(private val context: Context) {
    private val main = Handler(Looper.getMainLooper())

    val batterySaver: Flow<Boolean> = callbackFlow {
        val power = context.getSystemService(PowerManager::class.java)
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                offer(power.isPowerSaveMode)
            }
        }
        context.registerReceiver(receiver, IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED))
        offer(power.isPowerSaveMode)
        awaitClose { context.unregisterReceiver(receiver) }
    }.distinctUntilChanged()

    /* A Settings.System float, again whenever it changes. */
    fun systemFloat(name: String, default: Float): Flow<Float> = callbackFlow {
        val resolver = context.contentResolver
        val read = { Settings.System.getFloat(resolver, name, default) }
        val observer = object : ContentObserver(main) {
            override fun onChange(selfChange: Boolean) {
                offer(read())
            }
        }
        resolver.registerContentObserver(Settings.System.getUriFor(name), false, observer)
        offer(read())
        awaitClose { resolver.unregisterContentObserver(observer) }
    }.distinctUntilChanged()

    val refreshRate: Flow<Float> = callbackFlow {
        val displays = context.getSystemService(DisplayManager::class.java)
        val read = { displays.getDisplay(Display.DEFAULT_DISPLAY)?.refreshRate ?: 0f }
        val listener = object : DisplayManager.DisplayListener {
            override fun onDisplayChanged(id: Int) {
                if (id == Display.DEFAULT_DISPLAY) offer(read())
            }
            override fun onDisplayAdded(id: Int) {}
            override fun onDisplayRemoved(id: Int) {}
        }
        displays.registerDisplayListener(listener, main)
        offer(read())
        awaitClose { displays.unregisterDisplayListener(listener) }
    }.distinctUntilChanged()
}
