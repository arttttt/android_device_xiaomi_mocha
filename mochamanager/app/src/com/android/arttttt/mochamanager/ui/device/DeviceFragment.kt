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

package com.android.arttttt.mochamanager.ui.device

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.ui.common.Format
import com.android.arttttt.mochamanager.ui.common.Rows
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.text

class DeviceFragment : Fragment() {
    private val model by screenViewModel { DeviceViewModel(it.device) }
    private lateinit var rows: Rows

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View {
        rows = Rows(requireContext()).apply {
            row(MODEL, R.string.model)
            row(MEMORY, R.string.memory_variant)
            row(BUILD, R.string.build)
            row(KERNEL, R.string.kernel)
            row(WIFI, R.string.wifi)
            row(SERIAL, R.string.serial)
        }
        return rows.root
    }

    override fun onViewCreated(view: View, saved: Bundle?) {
        collectWhileShown(model.state, ::render)
    }

    private fun render(device: Loadable<DeviceInfo>) {
        rows[MODEL] = text(device) { it.model }
        rows[MEMORY] = text(device) { "${it.memoryGb} GB (${Format.mib(it.memTotalKb)})" }
        rows[BUILD] = text(device) { it.build }
        rows[KERNEL] = text(device) { it.kernel }
        rows[WIFI] = text(device) { it.wifi }
        rows[SERIAL] = text(device) { it.serial ?: getString(R.string.unknown) }
    }

    private companion object {
        const val MODEL = "model"
        const val MEMORY = "memory"
        const val BUILD = "build"
        const val KERNEL = "kernel"
        const val WIFI = "wifi"
        const val SERIAL = "serial"
    }
}
