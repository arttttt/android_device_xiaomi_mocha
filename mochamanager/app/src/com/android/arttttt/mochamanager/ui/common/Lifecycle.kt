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

package com.android.arttttt.mochamanager.ui.common

import androidx.fragment.app.Fragment
import androidx.fragment.app.viewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.android.arttttt.mochamanager.MochaManagerApp
import com.android.arttttt.mochamanager.di.AppContainer
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch

/*
 * Collects while the screen is on show. A tab that is not is held at STARTED,
 * so it stops collecting -- and its model, after a grace period, stops
 * asking for what nobody sees.
 */
fun <T> Fragment.collectWhileShown(flow: Flow<T>, action: suspend (T) -> Unit) {
    viewLifecycleOwner.lifecycleScope.launch {
        viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.RESUMED) {
            flow.collect(action)
        }
    }
}

/* A screen's ViewModel, made from the app's container. */
inline fun <reified VM : ViewModel> Fragment.screenViewModel(
    crossinline create: (AppContainer) -> VM,
) = viewModels<VM> {
    object : ViewModelProvider.Factory {
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            val container = (requireActivity().application as MochaManagerApp).container
            @Suppress("UNCHECKED_CAST")
            return create(container) as T
        }
    }
}
