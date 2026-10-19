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

package com.android.arttttt.mochamanager.ui.main

import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.ui.display.DisplayFragment
import com.android.arttttt.mochamanager.ui.overview.OverviewFragment
import com.android.arttttt.mochamanager.ui.performance.PerformanceFragment
import com.android.arttttt.mochamanager.ui.power.PowerFragment
import com.android.arttttt.mochamanager.ui.sensors.SensorsFragment

/* The tabs, their menu items and the screen each shows. */
enum class Tab(val itemId: Int, val create: () -> Fragment) {
    OVERVIEW(R.id.tab_overview, ::OverviewFragment),
    POWER(R.id.tab_power, ::PowerFragment),
    PERFORMANCE(R.id.tab_performance, ::PerformanceFragment),
    SENSORS(R.id.tab_sensors, ::SensorsFragment),
    DISPLAY(R.id.tab_display, ::DisplayFragment);

    val tag: String
        get() = "tab:$name"

    companion object {
        fun of(itemId: Int) = values().first { it.itemId == itemId }

        /* By name, as it is saved; null for one no longer there. */
        fun named(name: String?) = values().firstOrNull { it.name == name }
    }
}
