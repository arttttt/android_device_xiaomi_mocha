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

package com.android.arttttt.chargertile

import android.graphics.drawable.Icon
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import com.android.arttttt.mochamanager.client.MochaManager

/*
 * Whether the battery charges, as the user chooses it.
 *
 * The tile hands the choice to the mochamanager service, which records it as
 * persist.vendor.mocha.charger.disabled; init.tegra.rc hands it to the
 * charger, and loading the persistent properties at boot hands it over again
 * after a reboot.
 */
class ChargerTile : TileService() {

    override fun onStartListening() {
        super.onStartListening()
        updateTile()
    }

    override fun onClick() {
        super.onClick()

        val disabled = isDisabled() ?: return
        MochaManager.call { it.setChargingDisabled(!disabled) }
        updateTile()
    }

    private fun updateTile() {
        val tile = qsTile ?: return
        val disabled = isDisabled()

        tile.icon = Icon.createWithResource(this, R.drawable.ic_charger)
        tile.label = getString(R.string.charger_tile_label)
        when (disabled) {
            null -> {
                tile.state = Tile.STATE_UNAVAILABLE
                tile.subtitle = null
            }
            true -> {
                tile.state = Tile.STATE_INACTIVE
                tile.subtitle = getString(R.string.charger_state_disabled)
            }
            false -> {
                tile.state = Tile.STATE_ACTIVE
                tile.subtitle = getString(R.string.charger_state_enabled)
            }
        }
        tile.updateTile()
    }

    /* null while the service does not answer. */
    private fun isDisabled(): Boolean? = MochaManager.call { it.battery.chargingDisabled }
}
