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

import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.PerformanceProfile
import com.android.arttttt.mochamanager.domain.model.PowerSample
import com.android.arttttt.mochamanager.domain.usecase.PowerHistory

data class PowerState(
    val sample: Loadable<PowerSample> = Loadable.Loading,
    val history: PowerHistory = PowerHistory.EMPTY,
    val battery: Loadable<Battery> = Loadable.Loading,
    val profile: Loadable<PerformanceProfile> = Loadable.Loading,
    /* While it is on, the profile is held at power save. */
    val batterySaver: Boolean = false,
)

sealed class PowerAction {
    data class SetCharging(val enabled: Boolean) : PowerAction()
    data class SetProfile(val profile: PerformanceProfile) : PowerAction()
}

sealed class PowerEffect {
    object ChangeFailed : PowerEffect()
}
