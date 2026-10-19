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

import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.RefreshMode

data class DisplayState(
    val refreshRate: Loadable<Float> = Loadable.Loading,
    /* Ready(null) for a range none of the modes is. */
    val mode: Loadable<RefreshMode?> = Loadable.Loading,
)

sealed class DisplayAction {
    data class SetMode(val mode: RefreshMode) : DisplayAction()
}

sealed class DisplayEffect {
    object ChangeFailed : DisplayEffect()
}
