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

import android.content.Context
import android.os.DropBoxManager
import android.os.SELinux
import android.os.SystemProperties
import com.android.arttttt.mochamanager.domain.model.CrashKind
import com.android.arttttt.mochamanager.domain.model.CrashRecord
import com.android.arttttt.mochamanager.domain.model.LogBuffer
import com.android.arttttt.mochamanager.domain.model.LogKind
import com.android.arttttt.mochamanager.domain.model.LogLine
import com.android.arttttt.mochamanager.domain.repository.LogRepository
import java.io.IOException
import kotlin.concurrent.thread
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.channels.sendBlocking
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.withContext

/*
 * The logs through logcat, which the app may run for every process's lines:
 * READ_LOGS puts it in the log group. The kernel's log is logd's kernel
 * buffer -- logd takes the kernel's messages from boot on (ro.logd.kernel) --
 * with each line's time since boot, as dmesg has it. The records of crashes
 * are the drop box's, where the system files tombstones, ANRs, crashed apps
 * and the kernel log pstore kept of a bad end.
 */
class LogcatRepository(private val context: Context, private val io: CoroutineDispatcher) : LogRepository {
    /*
     * A thread of its own reads logcat: a read blocks until there is a line,
     * which a quiet kernel log may not have for minutes. Stopping is ending
     * the process, which ends the read.
     */
    override fun follow(kind: LogKind, buffers: Set<LogBuffer>): Flow<LogLine> = callbackFlow {
        val process = ProcessBuilder(command(kind, buffers) + listOf("-T", FOLLOW_FROM.toString()))
                .redirectErrorStream(true)
                .start()
        thread(name = "logcat", isDaemon = true) {
            try {
                process.inputStream.bufferedReader().forEachLine { sendBlocking(LogLine.parse(it)) }
            } catch (e: Exception) {
                /* The process ended, or nobody is listening any more. */
            }
            close()
        }
        awaitClose { process.destroy() }
    }

    override suspend fun dump(kind: LogKind, buffers: Set<LogBuffer>): String? = withContext(io) {
        try {
            val process = ProcessBuilder(command(kind, buffers) + "-d").redirectErrorStream(true).start()
            process.inputStream.bufferedReader().readText().also { process.waitFor() }
        } catch (e: IOException) {
            null
        }
    }

    override suspend fun crashes(kinds: Set<CrashKind>, limit: Int): List<CrashRecord> = withContext(io) {
        val dropBox = context.getSystemService(DropBoxManager::class.java)
        kinds.flatMap { kind -> TAGS.getValue(kind).flatMap { tag -> records(dropBox, kind, tag).takeLast(limit) } }
                .sortedBy { it.timeMs }
    }

    override suspend fun properties(): Map<String, String> = withContext(io) {
        PROPERTIES.associateWith { SystemProperties.get(it) }.filterValues { it.isNotEmpty() } +
                ("selinux" to if (SELinux.isSELinuxEnforced()) "enforcing" else "permissive")
    }

    private fun records(dropBox: DropBoxManager, kind: CrashKind, tag: String): List<CrashRecord> {
        val records = ArrayList<CrashRecord>()
        var after = 0L
        while (true) {
            val entry = dropBox.getNextEntry(tag, after) ?: break
            entry.use { e ->
                e.getText(MAX_RECORD)?.let { records += CrashRecord(kind, tag, e.timeMillis, it) }
                after = e.timeMillis
            }
        }
        return records
    }

    private fun command(kind: LogKind, buffers: Set<LogBuffer>): List<String> = when (kind) {
        LogKind.KERNEL -> listOf("logcat", "-b", "kernel", "-v", "monotonic")
        LogKind.SYSTEM -> listOf("logcat", "-b", buffers.joinToString(",") { it.id }, "-v", "threadtime")
    }

    private companion object {
        /* The lines a log opens with before the new ones. */
        const val FOLLOW_FROM = 1000
        const val MAX_RECORD = 512 * 1024

        val TAGS = mapOf(
                CrashKind.TOMBSTONE to listOf("SYSTEM_TOMBSTONE"),
                CrashKind.ANR to listOf("data_app_anr", "system_app_anr", "system_server_anr"),
                CrashKind.APP_CRASH to listOf("data_app_crash", "system_app_crash", "system_server_crash",
                        "data_app_native_crash", "system_app_native_crash"),
                CrashKind.LAST_KERNEL_LOG to listOf("SYSTEM_LAST_KMSG"))

        val PROPERTIES = listOf(
                "ro.build.fingerprint", "ro.build.type", "ro.build.date", "ro.lineage.version",
                "ro.boot.hardware", "ro.bootloader", "ro.logd.kernel")
    }
}
