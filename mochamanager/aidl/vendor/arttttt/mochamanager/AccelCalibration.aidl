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

/*
 * The accelerometer's calibration, as the sensors HAL takes it: an offset
 * the sensor hub subtracts, in the hub's counts and Android's axes, and a
 * scale per axis the HAL applies.
 */
@VintfStability
parcelable AccelCalibration {
    /* Whether there is one; the rest is the identity when not. */
    boolean present;
    int[] offset;
    float[] scale;
    /* When it was made, ms since the epoch; 0 when there is none. */
    long madeMs;
}
