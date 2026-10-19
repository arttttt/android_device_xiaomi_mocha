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

package com.android.arttttt.mochamanager

import android.os.Build

/* What the tablet is. */
class DeviceFragment : Screen(0) {

    override fun build() {
        rows.row("model", R.string.model)
        rows.row("memory", R.string.memory_variant)
        rows.row("build", R.string.build)
        rows.row("kernel", R.string.kernel)
        rows.row("wifi", R.string.wifi)
        rows.row("serial", R.string.serial)
    }

    override fun refresh() {
        rows["serial"] = try {
            Build.getSerial()
        } catch (e: SecurityException) {
            getString(R.string.unknown)
        }

        Service.load({ it.deviceInfo }) { d ->
            if (d == null) return@load unavailable()
            rows["model"] = d.model
            /* The 3 GB model is the one with more than 2 GiB for Linux, as
             * the heap settings used to tell them apart. */
            rows["memory"] = (if (d.memTotalKb > 2L * 1024 * 1024) "3 GB" else "2 GB") +
                    " (" + Format.mib(d.memTotalKb) + ")"
            rows["build"] = d.build
            rows["kernel"] = d.kernel
            rows["wifi"] = d.wifi
        }
    }
}
