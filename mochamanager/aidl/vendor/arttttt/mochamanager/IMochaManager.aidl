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

package vendor.arttttt.mochamanager;

import vendor.arttttt.mochamanager.AccelCalibration;
import vendor.arttttt.mochamanager.BatteryInfo;
import vendor.arttttt.mochamanager.DeviceInfo;
import vendor.arttttt.mochamanager.PowerSample;
import vendor.arttttt.mochamanager.Snapshot;

/*
 * The tablet's information and its switches, for MochaManager and the quick
 * settings tiles. The service holds every right; a client holds a binder.
 */
@VintfStability
interface IMochaManager {
    const int PROFILE_POWER_SAVE = 0;
    const int PROFILE_BALANCED = 1;
    const int PROFILE_PERFORMANCE = 2;
    const int PROFILE_EFFICIENCY = 3;

    DeviceInfo getDeviceInfo();
    BatteryInfo getBattery();
    /* A fresh conversion each call: poll it while the reading is on screen,
     * not otherwise. */
    PowerSample readPower();
    Snapshot readSnapshot();

    /* persist.vendor.mocha.charger.disabled; init hands it to the charger. */
    void setChargingDisabled(boolean disabled);

    /* The user's choice, persist.vendor.mocha.perf.profile, balanced until
     * there is one. */
    int getPerformanceProfile();
    /* The profile in force, sys.perf.profile: power save while the battery
     * saver lasts, whatever the choice. */
    int getActivePerformanceProfile();
    /* Records the choice; power.mocha.rc puts it in force unless the battery
     * saver holds power save. EX_ILLEGAL_ARGUMENT for an unknown profile. */
    void setPerformanceProfile(int profile);

    AccelCalibration getAccelCalibration();
    /* Written where the sensors HAL watches for it; it takes it at once.
     * EX_ILLEGAL_ARGUMENT for three offsets and three scales it would not
     * take: a scale more than a tenth off one is no calibration. */
    void setAccelCalibration(in AccelCalibration calibration);
    /* Back to the chip as it reads. */
    void resetAccelCalibration();
}
