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

package com.android.arttttt.mochamanager.ui.logs

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.di.AppContainer
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.LogBuffer
import com.android.arttttt.mochamanager.domain.model.LogFilter
import com.android.arttttt.mochamanager.domain.model.LogKind
import com.android.arttttt.mochamanager.domain.model.LogLevel
import com.android.arttttt.mochamanager.domain.model.LogLine
import com.android.arttttt.mochamanager.domain.repository.ArchiveRepository
import com.android.arttttt.mochamanager.domain.repository.DeviceRepository
import com.android.arttttt.mochamanager.domain.repository.LogRepository
import com.android.arttttt.mochamanager.domain.usecase.ArchivePart
import com.android.arttttt.mochamanager.domain.usecase.LogArchive
import com.android.arttttt.mochamanager.domain.usecase.tail
import com.android.arttttt.mochamanager.ui.common.Effects
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/* Which log is read. */
data class LogSource(
    val kind: LogKind = LogKind.KERNEL,
    val buffers: Set<LogBuffer> = setOf(LogBuffer.MAIN, LogBuffer.SYSTEM, LogBuffer.CRASH),
)

data class LogsState(
    val source: LogSource = LogSource(),
    val filter: LogFilter = LogFilter(),
    val paused: Boolean = false,
    /* The lines on show, the filter's. */
    val lines: List<LogLine> = emptyList(),
    val device: DeviceInfo? = null,
    /* An archive is being made. */
    val collecting: Boolean = false,
)

sealed class LogsAction {
    data class Read(val kind: LogKind) : LogsAction()
    data class Buffer(val buffer: LogBuffer, val on: Boolean) : LogsAction()
    data class Level(val level: LogLevel, val on: Boolean) : LogsAction()
    data class Search(val query: String) : LogsAction()
    object Pause : LogsAction()
    data class Export(val parts: Set<ArchivePart>, val redact: Boolean, val to: ArchiveTarget) : LogsAction()
}

enum class ArchiveTarget { SHARE, DOWNLOADS }

sealed class LogsEffect {
    data class Share(val zip: File) : LogsEffect()
    object Saved : LogsEffect()
    object Failed : LogsEffect()
}

/*
 * A log, followed while the screen is on show, its last lines kept; and the
 * archive of the logs. Paused, the screen keeps what it showed while the
 * log goes on being read, so that resuming misses nothing.
 */
class LogsViewModel(
    logs: LogRepository,
    private val archive: LogArchive,
    private val archives: ArchiveRepository,
    private val device: DeviceRepository,
) : ViewModel() {
    val effects = Effects<LogsEffect>()
    private val source = MutableStateFlow(LogSource())
    private val filter = MutableStateFlow(LogFilter())
    private val frozen = MutableStateFlow<List<LogLine>?>(null)
    private val info = MutableStateFlow<DeviceInfo?>(null)
    private val collecting = MutableStateFlow(false)
    private var latest: List<LogLine> = emptyList()

    private val live = source.flatMapLatest { s ->
        flow {
            emit(emptyList())
            emitAll(logs.follow(s.kind, s.buffers).tail(CAPACITY, PERIOD_MS))
        }
    }.onEach { latest = it }

    private val shown = combine(live, frozen, filter) { lines, held, f ->
        (held ?: lines).filter(f::accepts)
    }.flowOn(Dispatchers.Default)

    val state: StateFlow<LogsState> = combine(source, filter, frozen, shown, combine(info, collecting) { d, busy -> d to busy }) {
        s, f, held, lines, (device, busy) -> LogsState(s, f, held != null, lines, device, busy)
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), LogsState())

    init {
        viewModelScope.launch { info.value = device.deviceInfo() }
    }

    fun onAction(action: LogsAction) {
        when (action) {
            /* Another log is read from its start: nothing held of the last. */
            is LogsAction.Read -> {
                frozen.value = null
                source.value = source.value.copy(kind = action.kind)
            }
            is LogsAction.Buffer -> {
                val buffers = source.value.buffers.let { if (action.on) it + action.buffer else it - action.buffer }
                /* logcat reads at least one. */
                if (buffers.isNotEmpty()) {
                    frozen.value = null
                    source.value = source.value.copy(buffers = buffers)
                }
            }
            is LogsAction.Level -> filter.value = filter.value.let {
                it.copy(levels = if (action.on) it.levels + action.level else it.levels - action.level)
            }
            is LogsAction.Search -> filter.value = filter.value.copy(query = action.query.trim())
            LogsAction.Pause -> frozen.value = if (frozen.value == null) latest else null
            is LogsAction.Export -> export(action)
        }
    }

    private fun export(action: LogsAction.Export) {
        if (collecting.value) return
        collecting.value = true
        viewModelScope.launch {
            val files = archive.files(action.parts, action.redact)
            val zip = archives.write("mocha-logs-" + LogArchive.stamp(System.currentTimeMillis()) + ".zip", files)
            effects.send(when {
                zip == null -> LogsEffect.Failed
                action.to == ArchiveTarget.SHARE -> LogsEffect.Share(zip)
                archives.toDownloads(zip) -> LogsEffect.Saved
                else -> LogsEffect.Failed
            })
            collecting.value = false
        }
    }

    companion object {
        /* Lines kept: some minutes of a busy system log. */
        private const val CAPACITY = 5000
        private const val PERIOD_MS = 250L

        fun create(c: AppContainer) = LogsViewModel(c.logs, c.logArchive, c.archives, c.device)
    }
}
