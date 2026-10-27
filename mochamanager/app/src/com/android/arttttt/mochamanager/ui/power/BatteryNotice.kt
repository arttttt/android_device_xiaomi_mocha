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

package com.android.arttttt.mochamanager.ui.power

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.ui.main.MainActivity
import com.android.arttttt.mochamanager.ui.main.Tab

/*
 * The notification that stays while the tablet runs on its battery with the
 * cable in, so that it is not left so unawares. Shown and taken away as the
 * controls see the mode; it opens Power, where the mode is changed back.
 */
class BatteryNotice(context: Context) {
    private val context = context.applicationContext
    private val manager = this.context.getSystemService(NotificationManager::class.java)

    fun show(on: Boolean) {
        if (on == shown) return
        shown = on
        if (!on) {
            manager.cancel(ID)
            return
        }
        manager.createNotificationChannel(NotificationChannel(CHANNEL,
                context.getString(R.string.notice_channel), NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(context, 0, MainActivity.open(context, Tab.POWER),
                PendingIntent.FLAG_IMMUTABLE)
        manager.notify(ID, Notification.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_tab_power)
                .setContentTitle(context.getString(R.string.notice_battery_title))
                .setContentText(context.getString(R.string.notice_battery_text))
                .setOngoing(true)
                .setContentIntent(open)
                .build())
    }

    private companion object {
        const val CHANNEL = "power_mode"
        const val ID = 1
        /* For the whole process, as the notification is: both screens with
         * the controls show it. null until either has. */
        var shown: Boolean? = null
    }
}
