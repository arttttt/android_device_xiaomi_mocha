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

package com.android.arttttt.mochamanager.ui.overview

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.repository.DeviceRepository
import com.android.arttttt.mochamanager.domain.repository.MonitorRepository
import com.android.arttttt.mochamanager.domain.repository.PowerRepository
import com.android.arttttt.mochamanager.domain.repository.ProfileRepository
import com.android.arttttt.mochamanager.domain.usecase.poll
import com.android.arttttt.mochamanager.ui.common.Effects
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import com.android.arttttt.mochamanager.ui.performance.PerformanceViewModel
import com.android.arttttt.mochamanager.ui.power.PowerSwitches
import com.android.arttttt.mochamanager.ui.power.SwitchAction
import com.android.arttttt.mochamanager.ui.power.SwitchFailed
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/*
 * The tablet at a glance: the charge and the draw, the two switches, a line
 * from each section, and what the tablet is -- read once, it does not change.
 */
class OverviewViewModel(
    power: PowerRepository,
    profiles: ProfileRepository,
    monitor: MonitorRepository,
    device: DeviceRepository,
) : ViewModel() {
    val effects = Effects<SwitchFailed>()
    private val switches = PowerSwitches(power, profiles)

    private val info = poll(0) { device.deviceInfo() }
            .stateIn(viewModelScope, SharingStarted.Lazily, Loadable.Loading)

    val state: StateFlow<OverviewState> = combine(
            poll(PowerSwitches.PERIOD_MS) { power.sample() },
            switches.state,
            poll(PerformanceViewModel.PERIOD_MS) { monitor.snapshot() },
            info,
    ) { sample, (battery, s), snapshot, device ->
        OverviewState(sample, battery, s, snapshot, device)
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), OverviewState())

    fun onAction(action: SwitchAction) {
        viewModelScope.launch {
            if (!switches.apply(action)) effects.send(SwitchFailed)
        }
    }
}
