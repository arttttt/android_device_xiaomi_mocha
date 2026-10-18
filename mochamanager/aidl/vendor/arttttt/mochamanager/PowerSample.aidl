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

/* One reading of the INA231 on the battery line. */
@VintfStability
parcelable PowerSample {
    /* CLOCK_BOOTTIME. */
    long timestampMs;
    int voltageMv;
    /* Negative while the battery discharges. */
    int currentMa;
    /* Drawn from the battery: voltage by minus the current. */
    int powerMw;
    /* Drawn from the battery since boot, from the power.stats HAL; -1 when
     * it does not answer. */
    long energyUWs;
}
