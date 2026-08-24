/*
 * Copyright (C) 2026 The LineageOS Project
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

package org.lineageos.hardware;

import android.os.SystemProperties;
import android.util.Log;

import com.android.server.LocalServices;
import com.android.server.display.DisplayTransformManager;

import lineageos.hardware.DisplayMode;

/*
 * Display profiles for the panel, implemented in software.
 *
 * The classic implementations of this class write a mode id into a panel
 * sysfs (mDNIe and friends). This panel has no such block; what it has is
 * the display controller's colour matrix, which the composer feeds into
 * the CMU. Each profile is therefore a saturation matrix handed to
 * DisplayTransformManager on a level of its own, where it composes with
 * night display, calibration and the accessibility transforms in the
 * usual ascending order.
 */
public class DisplayModeControl {

    private static final String TAG = "DisplayModeControl";

    /*
     * Below NIGHT_DISPLAY (100) and everything else registered: the
     * profile is the panel's character, the base the other layers stack
     * onto.
     */
    private static final int LEVEL_COLOR_MATRIX_PROFILE = 50;

    /*
     * Written by system_server like the rest of the persist.sys family --
     * the classic dotfile under /data/misc would need its own sepolicy.
     */
    private static final String PROFILE_PROP = "persist.sys.hwc.display_profile";

    /*
     * Saturation matrices: column-major, Rec.709 luma weights, rows
     * summing to one so white stays white. No offset column and a
     * (0, 0, 0, 1) projective row. Standard is null -- the level is
     * removed from the composition rather than multiplied by identity.
     */
    private static final float[] MATRIX_CINEMA = {
            // saturation 1.35
            1.275f, -0.074f, -0.074f, 0f,
           -0.250f,  1.099f, -0.250f, 0f,
           -0.025f, -0.025f,  1.324f, 0f,
                0f,      0f,      0f, 1f
    };

    private static final float[] MATRIX_DYNAMIC = {
            // saturation 1.5
            1.394f, -0.106f, -0.106f, 0f,
           -0.358f,  1.142f, -0.358f, 0f,
           -0.036f, -0.036f,  1.464f, 0f,
                0f,      0f,      0f, 1f
    };

    /* Names come from the LineageParts dictionary and localize there. */
    private static final DisplayMode[] DISPLAY_MODES = {
            new DisplayMode(0, "Standard"),
            new DisplayMode(1, "Cinema"),
            new DisplayMode(2, "Dynamic"),
    };

    private static final float[][] MODE_MATRICES = {
            null, MATRIX_CINEMA, MATRIX_DYNAMIC,
    };

    private static int sCurrentId;
    private static boolean sApplied = false;

    static {
        /*
         * The stored profile is applied on class load: nothing in the
         * system asks this class anything at boot -- only the settings UI
         * does, so waiting for a caller would wait for the user. The
         * transform manager registers in the bootstrap phase and this
         * class loads at the tail of startOtherServices; should that
         * order ever change, the first real call gets a second chance.
         * A failure here must not poison the class -- that would take
         * the whole hardware service down with it.
         */
        try {
            sCurrentId = clampId(SystemProperties.getInt(PROFILE_PROP, 0));
            apply(sCurrentId);
        } catch (Throwable t) {
            Log.e(TAG, "profile not applied at load", t);
        }
    }

    private static int clampId(int id) {
        return (id < 0 || id >= DISPLAY_MODES.length) ? 0 : id;
    }

    private static synchronized boolean apply(int id) {
        DisplayTransformManager dtm =
                LocalServices.getService(DisplayTransformManager.class);
        if (dtm == null) {
            Log.w(TAG, "transform manager not up, profile deferred");
            return false;
        }
        dtm.setColorMatrix(LEVEL_COLOR_MATRIX_PROFILE, MODE_MATRICES[id]);
        sApplied = true;
        return true;
    }

    private static synchronized void applyIfDeferred() {
        if (!sApplied) {
            apply(sCurrentId);
        }
    }

    public static boolean isSupported() {
        return true;
    }

    public static DisplayMode[] getAvailableModes() {
        applyIfDeferred();
        return DISPLAY_MODES;
    }

    public static synchronized DisplayMode getCurrentMode() {
        applyIfDeferred();
        return DISPLAY_MODES[sCurrentId];
    }

    public static DisplayMode getDefaultMode() {
        return DISPLAY_MODES[clampId(SystemProperties.getInt(PROFILE_PROP, 0))];
    }

    public static synchronized boolean setMode(DisplayMode mode, boolean makeDefault) {
        if (mode == null || mode.id < 0 || mode.id >= DISPLAY_MODES.length) {
            return false;
        }
        if (!apply(mode.id)) {
            return false;
        }
        sCurrentId = mode.id;
        if (makeDefault) {
            SystemProperties.set(PROFILE_PROP, String.valueOf(mode.id));
        }
        return true;
    }
}
