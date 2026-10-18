/*
 * Copyright (C) 2017-2026 arttttt
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

package com.android.arttttt.performanceprofilestile

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.drawable.Icon
import android.os.PowerManager
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import com.android.arttttt.mochamanager.client.MochaManager
import vendor.arttttt.mochamanager.IMochaManager

/*
 * The performance profile, as the user chooses it.
 *
 * The tile hands the choice to the mochamanager service, which records it as
 * persist.vendor.mocha.perf.profile. power.mocha.rc puts it in force by
 * writing the governor's knobs, and holds power save over it while the
 * battery saver is on -- which is why the tile is unavailable then: a choice
 * made under the saver would not take effect until it ended.
 */
class PerformanceProfilesTile : TileService() {

    private val powerSaveReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) = updateTile()
    }

    private var listening = false

    override fun onStartListening() {
        super.onStartListening()

        /* Only while the panel shows the tile: nothing needs it redrawn when
         * nobody can see it, and it is read afresh on the next opening. */
        registerReceiver(powerSaveReceiver,
                IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED))
        listening = true
        updateTile()
    }

    override fun onStopListening() {
        if (listening) {
            unregisterReceiver(powerSaveReceiver)
            listening = false
        }
        super.onStopListening()
    }

    override fun onClick() {
        super.onClick()

        if (isPowerSaveMode()) return
        val profile = chosen() ?: return
        MochaManager.call { it.setPerformanceProfile(next(profile)) }
        updateTile()
    }

    private fun updateTile() {
        val tile = qsTile ?: return
        val profile = chosen()

        tile.icon = Icon.createWithResource(this, iconOf(profile ?: BALANCED))
        tile.label = getString(R.string.profiles_tile_label)
        when {
            profile == null -> {
                tile.state = Tile.STATE_UNAVAILABLE
                tile.subtitle = null
            }
            isPowerSaveMode() -> {
                tile.state = Tile.STATE_UNAVAILABLE
                tile.subtitle = getString(R.string.battery_saver_text)
            }
            else -> {
                tile.state = Tile.STATE_ACTIVE
                tile.subtitle = getString(nameOf(profile))
            }
        }
        tile.updateTile()
    }

    private fun isPowerSaveMode() = getSystemService(PowerManager::class.java).isPowerSaveMode

    /* The user's choice, balanced until there is one, as power.mocha.rc has
     * it; null while the service does not answer. */
    private fun chosen(): Int? =
            MochaManager.call { it.performanceProfile }?.let { if (it in ORDER) it else BALANCED }

    private companion object {
        const val POWER_SAVE = IMochaManager.PROFILE_POWER_SAVE
        const val BALANCED = IMochaManager.PROFILE_BALANCED
        const val PERFORMANCE = IMochaManager.PROFILE_PERFORMANCE
        const val EFFICIENCY = IMochaManager.PROFILE_EFFICIENCY

        /* Each press tries a step harder, and the hardest wraps round to the
         * lightest. */
        val ORDER = intArrayOf(POWER_SAVE, EFFICIENCY, BALANCED, PERFORMANCE)

        fun next(profile: Int) = ORDER[(ORDER.indexOf(profile) + 1) % ORDER.size]

        fun iconOf(profile: Int) = when (profile) {
            POWER_SAVE -> R.drawable.ic_profile_power_save
            EFFICIENCY -> R.drawable.ic_profile_bias_power_save
            PERFORMANCE -> R.drawable.ic_notification_profile_high
            else -> R.drawable.ic_profile_balanced
        }

        fun nameOf(profile: Int) = when (profile) {
            POWER_SAVE -> R.string.power_save_profile_text
            EFFICIENCY -> R.string.bias_power_save_profile_text
            PERFORMANCE -> R.string.high_performance_profile_text
            else -> R.string.balanced_profile_text
        }
    }
}
