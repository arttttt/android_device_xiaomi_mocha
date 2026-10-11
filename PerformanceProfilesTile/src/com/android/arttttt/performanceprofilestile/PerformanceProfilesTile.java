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
package com.android.arttttt.performanceprofilestile;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.drawable.Icon;
import android.os.PowerManager;
import android.os.SystemProperties;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/*
 * The performance profile, as the user chooses it.
 *
 * The tile only records the choice, in persist.sys.perf.profile.
 * power.mocha.rc puts it in force by writing the governor's knobs, and holds
 * power save over it while the battery saver is on -- which is why the tile
 * is unavailable then: a choice made under the saver would not take effect
 * until it ended.
 */
public class PerformanceProfilesTile extends TileService {

    private static final String PROPERTY = "persist.sys.perf.profile";

    private static final int POWER_SAVE = 0;
    private static final int BALANCED = 1;
    private static final int PERFORMANCE = 2;
    private static final int EFFICIENCY = 3;

    /* Each press tries a step harder, and the hardest wraps round to the
     * lightest. */
    private static final int[] ORDER = { POWER_SAVE, EFFICIENCY, BALANCED, PERFORMANCE };

    private final BroadcastReceiver mPowerSaveReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            updateTile();
        }
    };

    private boolean mListening;

    @Override
    public void onStartListening() {
        super.onStartListening();

        /* Only while the panel shows the tile: nothing needs it redrawn when
         * nobody can see it, and it is read afresh on the next opening. */
        registerReceiver(mPowerSaveReceiver,
                new IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED));
        mListening = true;
        updateTile();
    }

    @Override
    public void onStopListening() {
        if (mListening) {
            unregisterReceiver(mPowerSaveReceiver);
            mListening = false;
        }
        super.onStopListening();
    }

    @Override
    public void onClick() {
        super.onClick();

        if (isPowerSaveMode()) return;

        SystemProperties.set(PROPERTY, String.valueOf(next(chosen())));
        updateTile();
    }

    private void updateTile() {
        final Tile tile = getQsTile();
        if (tile == null) return;

        final int profile = chosen();
        tile.setIcon(Icon.createWithResource(this, iconOf(profile)));
        tile.setLabel(getString(R.string.profiles_tile_label));

        if (isPowerSaveMode()) {
            tile.setState(Tile.STATE_UNAVAILABLE);
            tile.setSubtitle(getString(R.string.battery_saver_text));
        } else {
            tile.setState(Tile.STATE_ACTIVE);
            tile.setSubtitle(getString(nameOf(profile)));
        }
        tile.updateTile();
    }

    private boolean isPowerSaveMode() {
        return getSystemService(PowerManager.class).isPowerSaveMode();
    }

    /* Balanced until the user has chosen, as power.mocha.rc has it. */
    private static int chosen() {
        final int profile = SystemProperties.getInt(PROPERTY, BALANCED);
        for (int known : ORDER) {
            if (known == profile) return profile;
        }
        return BALANCED;
    }

    private static int next(int profile) {
        for (int i = 0; i < ORDER.length; i++) {
            if (ORDER[i] == profile) return ORDER[(i + 1) % ORDER.length];
        }
        return BALANCED;
    }

    private static int iconOf(int profile) {
        switch (profile) {
            case POWER_SAVE:
                return R.drawable.ic_profile_power_save;
            case EFFICIENCY:
                return R.drawable.ic_profile_bias_power_save;
            case PERFORMANCE:
                return R.drawable.ic_notification_profile_high;
            default:
                return R.drawable.ic_profile_balanced;
        }
    }

    private static int nameOf(int profile) {
        switch (profile) {
            case POWER_SAVE:
                return R.string.power_save_profile_text;
            case EFFICIENCY:
                return R.string.bias_power_save_profile_text;
            case PERFORMANCE:
                return R.string.high_performance_profile_text;
            default:
                return R.string.balanced_profile_text;
        }
    }
}
