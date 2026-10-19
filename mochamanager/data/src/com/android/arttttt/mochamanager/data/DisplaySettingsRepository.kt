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

package com.android.arttttt.mochamanager.data

import android.content.Context
import android.provider.Settings
import com.android.arttttt.mochamanager.domain.model.RefreshMode
import com.android.arttttt.mochamanager.domain.repository.DisplayRepository
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.withContext

/*
 * The refresh rate range as the framework's own settings keep it,
 * Settings.System min and peak refresh rate; WRITE_SECURE_SETTINGS, which
 * the app is granted as a priv-app.
 */
class DisplaySettingsRepository(
    private val context: Context,
    private val system: SystemSource,
    private val io: CoroutineDispatcher,
) : DisplayRepository {
    override val refreshRate = system.refreshRate

    override val mode: Flow<RefreshMode?> = combine(
            system.systemFloat(Settings.System.MIN_REFRESH_RATE, 0f),
            system.systemFloat(Settings.System.PEAK_REFRESH_RATE, 60f)) { min, peak ->
        RefreshMode.of(min, peak)
    }

    override suspend fun setMode(mode: RefreshMode) = withContext(io) {
        val resolver = context.contentResolver
        try {
            Settings.System.putFloat(resolver, Settings.System.MIN_REFRESH_RATE, mode.min) &&
                    Settings.System.putFloat(resolver, Settings.System.PEAK_REFRESH_RATE, mode.peak)
        } catch (e: RuntimeException) {
            false
        }
    }
}
