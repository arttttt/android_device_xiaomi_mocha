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

package com.android.arttttt.mochamanager.domain.model

/* The two logs there are to read: the kernel's and the system's. */
enum class LogKind { KERNEL, SYSTEM }

/* The system log's buffers, as logcat names them. */
enum class LogBuffer(val id: String) {
    MAIN("main"),
    SYSTEM("system"),
    CRASH("crash"),
    EVENTS("events"),
}

/* A line's priority, coarser than logcat's: verbose goes with debug, fatal
 * and silent with error. */
enum class LogLevel { DEBUG, INFO, WARN, ERROR }

/* One line of a log as logcat prints it; level is null for a line it does
 * not have one on, such as a buffer's header. */
data class LogLine(val text: String, val level: LogLevel?) {
    companion object {
        /*
         * logcat's priority is the first one-letter field after the time and
         * the ids: "10-27 01:14:02.118  1203  1290 I Tag: ..." with
         * threadtime, "   871.738     0     0 I PM : ..." with monotonic.
         */
        fun parse(text: String): LogLine {
            val fields = text.trim().split(WHITESPACE, limit = 6)
            val level = fields.drop(1).take(4).mapNotNull { field ->
                if (field.length != 1) null else when (field[0]) {
                    'V', 'D' -> LogLevel.DEBUG
                    'I' -> LogLevel.INFO
                    'W' -> LogLevel.WARN
                    'E', 'F', 'A' -> LogLevel.ERROR
                    else -> null
                }
            }.firstOrNull()
            return LogLine(text, level)
        }

        private val WHITESPACE = Regex("\\s+")
    }
}

/* Which lines of a log are on show: of the levels asked for, and with the
 * text asked for in them, whatever its case. A line with no level shows. */
data class LogFilter(
    val levels: Set<LogLevel> = setOf(LogLevel.INFO, LogLevel.WARN, LogLevel.ERROR),
    val query: String = "",
) {
    fun accepts(line: LogLine): Boolean =
            (line.level == null || line.level in levels) &&
                    (query.isEmpty() || line.text.contains(query, ignoreCase = true))
}

/* What the system keeps of a crash, an ANR or the last boot's end. */
enum class CrashKind { TOMBSTONE, ANR, APP_CRASH, LAST_KERNEL_LOG }

/* One such record, with when it was made. */
data class CrashRecord(val kind: CrashKind, val tag: String, val timeMs: Long, val text: String)
