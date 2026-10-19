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

package com.android.arttttt.mochamanager.ui.display

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.repository.DisplayRepository
import com.android.arttttt.mochamanager.ui.common.Effects
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/* The refresh rate and the range it may be chosen from; both change on
 * their own, so both are listened to rather than polled. */
class DisplayViewModel(private val display: DisplayRepository) : ViewModel() {
    val effects = Effects<DisplayEffect>()

    val state: StateFlow<DisplayState> = combine(display.refreshRate, display.mode) { rate, mode ->
        DisplayState(Loadable.Ready(rate), Loadable.Ready(mode))
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), DisplayState())

    fun onAction(action: DisplayAction) {
        when (action) {
            is DisplayAction.SetMode -> viewModelScope.launch {
                if (!display.setMode(action.mode)) effects.send(DisplayEffect.ChangeFailed)
            }
        }
    }
}
