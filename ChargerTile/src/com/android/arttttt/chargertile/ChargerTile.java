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
package com.android.arttttt.chargertile;

import android.graphics.drawable.Icon;
import android.os.SystemProperties;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/*
 * Whether the battery charges, as the user chooses it.
 *
 * The tile only records the choice, in persist.sys.charger.disabled;
 * init.tegra.rc hands it to the charger, and loading the persistent
 * properties at boot hands it over again after a reboot.
 */
public class ChargerTile extends TileService {

    private static final String PROPERTY = "persist.sys.charger.disabled";

    @Override
    public void onStartListening() {
        super.onStartListening();
        updateTile();
    }

    @Override
    public void onClick() {
        super.onClick();

        SystemProperties.set(PROPERTY, isDisabled() ? "0" : "1");
        updateTile();
    }

    private void updateTile() {
        final Tile tile = getQsTile();
        if (tile == null) return;

        final boolean disabled = isDisabled();
        tile.setIcon(Icon.createWithResource(this, R.drawable.ic_charger));
        tile.setLabel(getString(R.string.charger_tile_label));
        tile.setSubtitle(getString(disabled ? R.string.charger_state_disabled
                                            : R.string.charger_state_enabled));
        tile.setState(disabled ? Tile.STATE_INACTIVE : Tile.STATE_ACTIVE);
        tile.updateTile();
    }

    private static boolean isDisabled() {
        return SystemProperties.getBoolean(PROPERTY, false);
    }
}
