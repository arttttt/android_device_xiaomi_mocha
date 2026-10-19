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

package com.android.arttttt.mochamanager.ui.power

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.domain.repository.PowerRepository
import com.android.arttttt.mochamanager.domain.repository.ProfileRepository
import com.android.arttttt.mochamanager.domain.usecase.History
import com.android.arttttt.mochamanager.domain.usecase.poll
import com.android.arttttt.mochamanager.ui.common.Effects
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.withIndex
import kotlinx.coroutines.launch

/* The battery, what it gives, and the two switches. */
class PowerViewModel(
    private val power: PowerRepository,
    profiles: ProfileRepository,
) : ViewModel() {
    val effects = Effects<SwitchFailed>()
    private val switches = PowerSwitches(power, profiles)

    /* Kept here, not in the flow, so leaving the tab keeps the two minutes. */
    private val history = MutableStateFlow(History.empty(PowerGraphView.CAPACITY))

    /*
     * The first reading after the tab is shown measures the tab being shown
     * -- the layout, the first frames -- and would set the scale for the two
     * minutes after it. It goes in the figures, not the graph.
     */
    private val samples = poll(PowerSwitches.PERIOD_MS) { power.sample() }
            .withIndex()
            .onEach { (i, sample) ->
                val mw = sample.valueOrNull?.powerMw
                if (i > 0 && mw != null) history.value = history.value.plus(mw.toFloat())
            }
            .map { it.value }

    val state: StateFlow<PowerState> = combine(samples, history, switches.state) { sample, history, (battery, s) ->
        PowerState(sample, history, battery, s)
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), PowerState())

    fun onAction(action: SwitchAction) {
        viewModelScope.launch {
            if (!switches.apply(action)) effects.send(SwitchFailed)
        }
    }
}
