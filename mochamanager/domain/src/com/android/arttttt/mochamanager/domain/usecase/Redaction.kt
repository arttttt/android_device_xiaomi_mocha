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

package com.android.arttttt.mochamanager.domain.usecase

/*
 * What a log shared with someone should not carry: the tablet's serial
 * number, and the hardware addresses of its radios and of the networks it
 * saw. An address keeps its first half, the maker's part, which says which
 * chip it was and nothing of whose. Accounts and network names can stay in a
 * log; this does not find them.
 */
class Redaction(private val serial: String?) {
    fun apply(text: String): String {
        var out = MAC.replace(text) { it.groupValues[1] + ":xx:xx:xx" }
        if (!serial.isNullOrBlank() && serial.length >= MIN_SERIAL) out = out.replace(serial, SERIAL_MASK)
        return out
    }

    private companion object {
        val MAC = Regex("(?i)\\b([0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2})(?::[0-9a-f]{2}){3}\\b")
        /* Shorter than this, it would be found in ordinary words and numbers. */
        const val MIN_SERIAL = 6
        const val SERIAL_MASK = "<serial>"
    }
}
