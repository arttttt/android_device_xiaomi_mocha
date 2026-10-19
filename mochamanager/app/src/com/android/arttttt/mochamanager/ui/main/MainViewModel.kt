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

package com.android.arttttt.mochamanager.ui.main

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/* The tab on show, kept across a rotation and across the process being
 * killed in the background. A screen may open another: the overview's tiles
 * open their sections. */
class MainViewModel(private val saved: SavedStateHandle) : ViewModel() {
    private val tab = MutableStateFlow(Tab.named(saved.get<String>(KEY)) ?: Tab.OVERVIEW)
    val state: StateFlow<Tab> = tab

    fun select(selected: Tab) {
        tab.value = selected
        saved.set(KEY, selected.name)
    }

    private companion object {
        const val KEY = "tab"
    }
}
