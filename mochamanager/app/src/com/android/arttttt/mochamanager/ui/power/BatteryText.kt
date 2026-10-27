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

import android.content.Context
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.PowerMode
import com.android.arttttt.mochamanager.domain.model.Supply
import com.android.arttttt.mochamanager.domain.usecase.Level

/* The battery's words, which the kernel gives in English, in the user's. */
object BatteryText {
    fun status(context: Context, b: Battery): String {
        /* The gauge has it discharging, which is right but not why. */
        if (b.powerMode == PowerMode.BATTERY) return context.getString(R.string.status_on_battery)
        val status = when (b.status) {
            "Charging" -> context.getString(R.string.status_charging)
            "Discharging" -> context.getString(R.string.status_discharging)
            "Not charging" -> context.getString(R.string.status_not_charging)
            "Full" -> context.getString(R.string.status_full)
            else -> b.status
        }
        return if (b.supply == Supply.NONE) status else status + " · " + supply(context, b)
    }

    /* Green while it charges or is full: the state one wants to see. */
    fun statusLevel(b: Battery): Level? = when {
        b.powerMode == PowerMode.BATTERY -> Level.WARN
        b.status == "Charging" || b.status == "Full" -> Level.GOOD
        else -> null
    }

    fun supply(context: Context, b: Battery): String = context.getString(when (b.supply) {
        Supply.AC -> R.string.supply_ac
        Supply.USB -> R.string.supply_usb
        Supply.NONE -> R.string.supply_none
    })

    fun health(context: Context, b: Battery): String =
            if (b.health == "Good") context.getString(R.string.health_good) else b.health

    fun healthLevel(b: Battery): Level = if (b.health == "Good") Level.GOOD else Level.BAD
}
