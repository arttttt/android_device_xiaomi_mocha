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

import java.util.Locale

/* Units as the screens show them. */
object Format {
    private fun f(pattern: String, vararg args: Any) = String.format(Locale.getDefault(), pattern, *args)

    fun mhz(khz: Int) = f("%d MHz", khz / 1000)
    fun mhzHz(hz: Long) = f("%d MHz", hz / 1_000_000)
    fun volts(uv: Int) = f("%.3f V", uv / 1e6)
    fun milliamps(ua: Int) = f("%d mA", ua / 1000)
    fun mah(uah: Int) = f("%d mAh", uah / 1000)
    fun celsius(milli: Int) = f("%.1f °C", milli / 1000.0)
    fun deciCelsius(deci: Int) = f("%.1f °C", deci / 10.0)
    fun percent(value: Double) = f("%.1f %%", value)
    fun mw(mw: Int) = f("%d mW", mw)
    fun wh(uws: Long) = f("%.3f Wh", uws / 3.6e9)
    fun mib(kb: Long) = f("%d MB", kb / 1024)
    fun ratio(value: Double) = f("%.1f×", value)
}
