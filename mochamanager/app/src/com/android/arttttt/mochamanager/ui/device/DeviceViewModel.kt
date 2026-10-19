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

package com.android.arttttt.mochamanager.ui.device

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.repository.DeviceRepository
import com.android.arttttt.mochamanager.domain.usecase.poll
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn

/* What the tablet is: read once, kept for as long as the screen lives. */
class DeviceViewModel(repository: DeviceRepository) : ViewModel() {
    val state: StateFlow<Loadable<DeviceInfo>> = poll(0) { repository.deviceInfo() }
            .stateIn(viewModelScope, SharingStarted.Lazily, Loadable.Loading)
}
