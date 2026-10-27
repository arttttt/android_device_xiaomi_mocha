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

package com.android.arttttt.mochamanager.domain

import com.android.arttttt.mochamanager.domain.model.LogFilter
import com.android.arttttt.mochamanager.domain.model.LogLevel
import com.android.arttttt.mochamanager.domain.model.LogLine
import com.android.arttttt.mochamanager.domain.usecase.ArchiveFile
import com.android.arttttt.mochamanager.domain.usecase.LogArchive
import com.android.arttttt.mochamanager.domain.usecase.Redaction
import com.android.arttttt.mochamanager.domain.usecase.tail
import kotlinx.coroutines.flow.asFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.TimeZone

class LogsTest {
    @Test
    fun levelsAsLogcatPrintsThem() {
        assertEquals(LogLevel.INFO,
                LogLine.parse("10-27 01:14:02.118  1203  1290 I CameraService: open").level)
        assertEquals(LogLevel.ERROR,
                LogLine.parse("10-27 01:14:06.010  1203  1290 F libc    : Fatal signal").level)
        assertEquals(LogLevel.DEBUG,
                LogLine.parse("10-27 01:14:06.010  1203  1290 V Tag: x").level)
        assertEquals(LogLevel.WARN,
                LogLine.parse("   871.738     0     0 W tegradc : underflow").level)
        assertNull(LogLine.parse("--------- beginning of kernel").level)
    }

    @Test
    fun aFilterTakesTheLevelsAndTheText() {
        val error = LogLine.parse("10-27 01:14:06.010  1203  1290 E CameraDevice: flush timeout")
        val debug = LogLine.parse("10-27 01:14:06.010  1203  1290 D Camera: frame")
        val header = LogLine.parse("--------- beginning of main")
        val filter = LogFilter()
        assertTrue(filter.accepts(error))
        assertFalse(filter.accepts(debug))
        assertTrue(filter.accepts(header))
        assertTrue(LogFilter(query = "CAMERA").accepts(error))
        assertFalse(LogFilter(query = "wifi").accepts(error))
    }

    @Test
    fun redactionHidesTheSerialAndHalfOfEachAddress() {
        val r = Redaction("1413E094")
        assertEquals("serial <serial>, bssid 00:90:4c:xx:xx:xx",
                r.apply("serial 1413E094, bssid 00:90:4c:0f:f1:5A"))
        /* A serial too short would be found everywhere: left alone. */
        assertEquals("abc 1234", Redaction("1234").apply("abc 1234"))
        assertEquals("no address here", Redaction(null).apply("no address here"))
    }

    @Test
    fun aTailKeepsTheLastLines() = runBlocking {
        val lines = (1..5).map { LogLine("line $it", LogLevel.INFO) }
        val got = lines.asFlow().tail(capacity = 3, periodMs = 10).first { it.size == 3 && it.last() == lines[4] }
        assertEquals(lines.takeLast(3), got)
    }

    @Test
    fun aRepeatedPathIsNumbered() {
        val files = listOf(ArchiveFile("crashes/t.txt", "1"), ArchiveFile("crashes/t.txt", "2"),
                ArchiveFile("dmesg.txt", "3"), ArchiveFile("crashes/t.txt", "4"))
        assertEquals(listOf("crashes/t.txt", "crashes/t-2.txt", "dmesg.txt", "crashes/t-3.txt"),
                LogArchive.unique(files).map { it.path })
    }

    @Test
    fun stampsInTheZoneAsked() {
        assertEquals("19700101-000001", LogArchive.stamp(1000, TimeZone.getTimeZone("UTC")))
        assertEquals("19700101-030001", LogArchive.stamp(1000, TimeZone.getTimeZone("GMT+3")))
    }
}
