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
import com.android.arttttt.mochamanager.domain.model.PowerMode
import com.android.arttttt.mochamanager.domain.repository.PowerRepository
import com.android.arttttt.mochamanager.domain.repository.ProfileRepository
import com.android.arttttt.mochamanager.domain.usecase.poll
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.combine

/* The power mode and the profile as the controls show them. */
data class Switches(
    val mode: PowerMode? = null,
    val profile: PerformanceProfile? = null,
    /* While it is on, the profile is held at power save. */
    val batterySaver: Boolean = false,
)

/* What the user does to them, on either screen. */
sealed class SwitchAction {
    data class SetMode(val mode: PowerMode) : SwitchAction()
    data class SetProfile(val profile: PerformanceProfile) : SwitchAction()
}

/* The one thing a screen with them is told: a change did not take. */
object SwitchFailed

/*
 * The battery and the profile, read every second, and the two switches over
 * them -- the overview and Power both have them, each model holding one of
 * these. A change is read back at once rather than on the next second.
 */
class PowerSwitches(
    private val power: PowerRepository,
    private val profiles: ProfileRepository,
) {
    private val refresh = MutableSharedFlow<Unit>(extraBufferCapacity = 1)

    /* The battery as read, and the switches it and the profile set. */
    val state: Flow<Pair<Loadable<Battery>, Switches>> = combine(
            poll(PERIOD_MS, refresh) { power.battery() },
            poll(PERIOD_MS, refresh) { profiles.profile() },
            profiles.batterySaver,
    ) { battery, profile, saver ->
        battery to Switches(battery.valueOrNull?.powerMode, profile.valueOrNull, saver)
    }

    /* Whether it took. */
    suspend fun apply(action: SwitchAction): Boolean {
        val done = when (action) {
            is SwitchAction.SetMode -> power.setPowerMode(action.mode)
            is SwitchAction.SetProfile -> profiles.setProfile(action.profile)
        }
        refresh.emit(Unit)
        return done
    }

    companion object {
        const val PERIOD_MS = 1000L
    }
}
