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

/* The fuel gauge and the charger. */
@VintfStability
parcelable BatteryInfo {
    int capacityPct;
    @utf8InCpp String status;
    @utf8InCpp String health;
    int voltageUv;
    /* Positive while the battery discharges, as the gauge reports it. */
    int currentUa;
    int tempDeciC;
    int chargeNowUah;
    int chargeFullUah;
    int chargeFullDesignUah;
    int cycleCount;
    boolean usbOnline;
    boolean acOnline;
    /* The user's switch, persist.vendor.mocha.charger.disabled. */
    boolean chargingDisabled;
}
