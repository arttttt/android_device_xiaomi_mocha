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

package com.android.arttttt.mochamanager.ui.monitor

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.domain.repository.MonitorRepository
import com.android.arttttt.mochamanager.domain.usecase.poll
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn

/* Where the tablet's effort goes right now, every two seconds. */
class MonitorViewModel(repository: MonitorRepository) : ViewModel() {
    val state: StateFlow<Loadable<Snapshot>> = poll(2000) { repository.snapshot() }
            .stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), Loadable.Loading)
}
