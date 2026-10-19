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

package com.android.arttttt.mochamanager.ui.performance

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.domain.repository.MonitorRepository
import com.android.arttttt.mochamanager.domain.usecase.History
import com.android.arttttt.mochamanager.domain.usecase.poll
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn

data class PerformanceState(
    val snapshot: Loadable<Snapshot> = Loadable.Loading,
    /* The busiest core's clock, kHz, over the last minute. */
    val cpu: History = History.empty(CPU_SAMPLES),
)

private const val CPU_SAMPLES = 30

/* Where the tablet's effort goes right now, every two seconds. */
class PerformanceViewModel(monitor: MonitorRepository) : ViewModel() {
    private val cpu = MutableStateFlow(History.empty(CPU_SAMPLES))

    val state: StateFlow<PerformanceState> = combine(
            poll(PERIOD_MS) { monitor.snapshot() }.onEach { s ->
                s.valueOrNull?.let { cpu.value = cpu.value.plus(it.busiestKhz.toFloat()) }
            },
            cpu,
    ) { snapshot, history -> PerformanceState(snapshot, history) }
            .stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), PerformanceState())

    companion object {
        const val PERIOD_MS = 2000L
    }
}
