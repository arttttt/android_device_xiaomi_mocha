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

package com.android.arttttt.mochamanager.ui.overview

import com.android.arttttt.mochamanager.domain.model.Battery
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.PowerSample
import com.android.arttttt.mochamanager.domain.model.Snapshot
import com.android.arttttt.mochamanager.ui.power.Switches

data class OverviewState(
    val sample: Loadable<PowerSample> = Loadable.Loading,
    val battery: Loadable<Battery> = Loadable.Loading,
    val switches: Switches = Switches(),
    val snapshot: Loadable<Snapshot> = Loadable.Loading,
    val device: Loadable<DeviceInfo> = Loadable.Loading,
)
