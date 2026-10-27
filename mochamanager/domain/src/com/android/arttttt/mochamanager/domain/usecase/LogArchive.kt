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

import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.CrashKind
import com.android.arttttt.mochamanager.domain.model.CrashRecord
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.LogBuffer
import com.android.arttttt.mochamanager.domain.model.LogKind
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.domain.repository.DeviceRepository
import com.android.arttttt.mochamanager.domain.repository.LogRepository
import com.android.arttttt.mochamanager.domain.repository.MonitorRepository
import com.android.arttttt.mochamanager.domain.repository.PowerRepository
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/* What a log archive can hold; the archive sheet lists them. */
enum class ArchivePart { SYSTEM_LOG, KERNEL_LOG, LAST_KERNEL_LOG, CRASHES, DEVICE }

/* A file of the archive: its path in it and what it holds. */
data class ArchiveFile(val path: String, val text: String)

/*
 * The files of a log archive, of the parts asked for. The system log is
 * every buffer of it there is; the crashes are the system's own records of
 * tombstones, ANRs and apps that crashed, the last few of each; the last
 * kernel log is what the kernel left in pstore when the boot before this one
 * ended badly. A part with nothing in it is no file.
 */
class LogArchive(
    private val logs: LogRepository,
    private val device: DeviceRepository,
    private val power: PowerRepository,
    private val monitor: MonitorRepository,
) {
    suspend fun files(parts: Set<ArchivePart>, redact: Boolean): List<ArchiveFile> {
        val info = device.deviceInfo()
        val files = ArrayList<ArchiveFile>()
        if (ArchivePart.SYSTEM_LOG in parts) {
            logs.dump(LogKind.SYSTEM, LogBuffer.values().toSet())?.let { files += ArchiveFile("logcat.txt", it) }
        }
        if (ArchivePart.KERNEL_LOG in parts) {
            logs.dump(LogKind.KERNEL, emptySet())?.let { files += ArchiveFile("dmesg.txt", it) }
        }
        if (ArchivePart.LAST_KERNEL_LOG in parts) {
            files += logs.crashes(setOf(CrashKind.LAST_KERNEL_LOG), CRASHES_EACH).map { record(it, "pstore") }
        }
        if (ArchivePart.CRASHES in parts) {
            val kinds = setOf(CrashKind.TOMBSTONE, CrashKind.ANR, CrashKind.APP_CRASH)
            files += logs.crashes(kinds, CRASHES_EACH).map { record(it, "crashes") }
        }
        if (ArchivePart.DEVICE in parts) {
            files += ArchiveFile("device.txt",
                    deviceReport(info, power.battery(), monitor.snapshot(), logs.properties()))
        }
        val named = unique(files)
        if (!redact) return named
        val redaction = Redaction(info?.serial)
        return named.map { it.copy(text = redaction.apply(it.text)) }
    }

    private fun record(r: CrashRecord, dir: String) = ArchiveFile("$dir/${r.tag}-${stamp(r.timeMs)}.txt", r.text)

    companion object {
        const val CRASHES_EACH = 5

        /* A ZIP takes a path once. Records are named by their time to the
         * second, and the system files two of a crash in the same second as
         * often as not: a second one becomes name-2, a third name-3. */
        fun unique(files: List<ArchiveFile>): List<ArchiveFile> {
            val seen = HashMap<String, Int>()
            return files.map { f ->
                val n = (seen[f.path] ?: 0) + 1
                seen[f.path] = n
                if (n == 1) f else f.copy(path = f.path.substringBeforeLast('.') + "-$n." + f.path.substringAfterLast('.'))
            }
        }

        /* A time as a file name has it: 20261027-011402, by the tablet's
         * clock unless told otherwise. */
        fun stamp(ms: Long, zone: TimeZone = TimeZone.getDefault()): String =
                SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT)
                        .apply { timeZone = zone }
                        .format(Date(ms))

        /* The tablet as it was when the logs were taken, for whoever reads
         * them without it in hand. */
        fun deviceReport(info: DeviceInfo?, battery: Battery?, snapshot: Snapshot?, props: Map<String, String>) =
                buildString {
                    info?.let {
                        appendLine("model: ${it.model}")
                        appendLine("build: ${it.build}")
                        appendLine("kernel: ${it.kernel}")
                        appendLine("wifi: ${it.wifi}")
                        appendLine("memory: ${it.memTotalKb} kB")
                    }
                    battery?.let {
                        appendLine("battery: ${it.capacityPct} % ${it.status}, ${it.voltageUv} uV, ${it.currentUa} uA")
                        appendLine("supply: ${it.supply}, power mode: ${it.powerMode}")
                    }
                    snapshot?.let { s ->
                        appendLine("cluster: ${s.cluster}")
                        for (c in s.cores) {
                            appendLine("cpu${c.id}: " + if (c.online) "${c.curKhz} kHz ${c.governor}" else "offline")
                        }
                        s.clusterTime?.let {
                            appendLine("cluster time: G ${it.gMs} ms, LP ${it.lpMs} ms, ${it.switches} switches")
                        }
                        appendLine("gpu: ${s.gpu.curHz} Hz, load ${s.gpu.loadPermille} permille")
                        for (t in s.thermals) appendLine("thermal ${t.name}: ${t.milliCelsius} mC")
                    }
                    if (props.isNotEmpty()) appendLine()
                    for ((key, value) in props.toSortedMap()) appendLine("[$key]: [$value]")
                }
    }
}
