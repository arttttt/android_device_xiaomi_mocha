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
 * How long each cluster has run, as cpuquiet counts it. The kernel keeps the
 * count only while cpuquiet switches the clusters itself; present is false
 * when it keeps none, and the rest is then 0.
 */
@VintfStability
parcelable ClusterTime {
    boolean present;
    long gMs;
    long lpMs;
    /* Switches between the clusters, either way. */
    int switches;
}
