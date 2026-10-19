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

import android.content.Context
import com.android.arttttt.mochamanager.R
import java.util.Locale

/* Readings as the screens show them, in the units and the decimal mark of
 * the user's language. */
class Units(private val context: Context) {
    private fun s(id: Int, vararg args: Any) = context.getString(id, *args)

    /* A bare number for a big value set beside its unit. */
    fun number(pattern: String, value: Double) = String.format(Locale.getDefault(), pattern, value)

    fun watts(mw: Int) = s(R.string.fmt_watts, mw / 1000.0)
    fun volts(uv: Int) = s(R.string.fmt_volts, uv / 1e6)
    fun milliamps(ua: Int) = s(R.string.fmt_milliamps, ua / 1000)
    fun mah(uah: Int) = s(R.string.fmt_mah, uah / 1000)
    fun wh(uws: Long) = s(R.string.fmt_wh, uws / 3.6e9)
    fun mhz(khz: Int) = s(R.string.fmt_mhz, khz / 1000)
    fun mhzOfHz(hz: Long) = s(R.string.fmt_mhz, (hz / 1_000_000).toInt())
    fun celsius(milli: Int) = s(R.string.fmt_celsius, milli / 1000.0)
    fun deciCelsius(deci: Int) = s(R.string.fmt_celsius, deci / 10.0)
    fun degreesShort(milli: Int) = s(R.string.fmt_degrees_short, milli / 1000.0)
    fun percent(value: Double) = s(R.string.fmt_percent, value)
    fun percentFine(value: Double) = s(R.string.fmt_percent_1, value)
    fun mb(kb: Long) = s(R.string.fmt_mb, (kb / 1024).toInt())
    fun gb(gb: Int) = s(R.string.fmt_gb, gb)
    fun hz(value: Float) = s(R.string.fmt_hz, value.toDouble())
    fun ratio(value: Double) = s(R.string.fmt_ratio, value)
    fun lux(value: Float) = s(R.string.fmt_lux, value.toDouble())
    fun microtesla(value: Float) = s(R.string.fmt_microtesla, value.toDouble())
    fun ms2(value: Float) = s(R.string.fmt_ms2, value.toDouble())
    fun rads(value: Float) = s(R.string.fmt_rads, value.toDouble())
    fun angle(value: Float) = s(R.string.fmt_angle, value.toDouble())
}
