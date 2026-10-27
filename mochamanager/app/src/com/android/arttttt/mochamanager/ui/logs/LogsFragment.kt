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

package com.android.arttttt.mochamanager.ui.logs

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.FileProvider
import androidx.core.widget.doAfterTextChanged
import androidx.fragment.app.Fragment
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.DeviceInfo
import com.android.arttttt.mochamanager.domain.model.LogBuffer
import com.android.arttttt.mochamanager.domain.model.LogKind
import com.android.arttttt.mochamanager.domain.model.LogLevel
import com.android.arttttt.mochamanager.domain.usecase.Level
import com.android.arttttt.mochamanager.ui.common.chip
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.header
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.android.arttttt.mochamanager.ui.common.subtitle
import com.google.android.material.button.MaterialButtonToggleGroup
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup
import com.google.android.material.floatingactionbutton.ExtendedFloatingActionButton
import java.io.File

class LogsFragment : Fragment(R.layout.fragment_logs) {
    private val model by screenViewModel(create = LogsViewModel::create)
    private var views: Views? = null

    private class Views(root: View) {
        val kinds: MaterialButtonToggleGroup = root.findViewById(R.id.kinds)
        val search: TextView = root.findViewById(R.id.search)
        val bufferGroup: ChipGroup = root.findViewById(R.id.buffers)
        val levels: MaterialButtonToggleGroup = root.findViewById(R.id.levels)
        val live: TextView = root.findViewById(R.id.live)
        val lines: RecyclerView = root.findViewById(R.id.lines)
        val pause: ExtendedFloatingActionButton = root.findViewById(R.id.pause)
        val export: View = root.findViewById(R.id.export)
        val adapter = LogAdapter()
        val buffers = LinkedHashMap<LogBuffer, Chip>()
    }

    /* Set while the views are brought in line with the state, so their
     * listeners do not take it for the user. */
    private var rendering = false

    override fun onViewCreated(view: View, saved: Bundle?) {
        view.header(R.string.tab_logs)
        val v = Views(view)
        views = v
        v.lines.layoutManager = LinearLayoutManager(requireContext()).apply { stackFromEnd = true }
        v.lines.adapter = v.adapter
        v.kinds.addOnButtonCheckedListener { _, id, checked ->
            if (!rendering && checked) {
                model.onAction(LogsAction.Read(if (id == R.id.kind_kernel) LogKind.KERNEL else LogKind.SYSTEM))
            }
        }
        for (buffer in LogBuffer.values()) {
            val chip = LayoutInflater.from(requireContext())
                    .inflate(R.layout.item_buffer_chip, v.bufferGroup, false) as Chip
            chip.text = buffer.id
            chip.setOnCheckedChangeListener { _, on -> if (!rendering) model.onAction(LogsAction.Buffer(buffer, on)) }
            v.bufferGroup.addView(chip)
            v.buffers[buffer] = chip
        }
        v.levels.addOnButtonCheckedListener { _, id, on ->
            LEVELS.entries.firstOrNull { it.value == id }?.key?.let { level ->
                if (!rendering) model.onAction(LogsAction.Level(level, on))
            }
        }
        v.search.doAfterTextChanged { if (!rendering) model.onAction(LogsAction.Search(it?.toString() ?: "")) }
        v.pause.setOnClickListener { model.onAction(LogsAction.Pause) }
        v.export.setOnClickListener { ArchiveSheet.ask(this) }
        collectWhileShown(model.state, ::render)
        collectWhileShown(model.effects.flow, ::onEffect)
    }

    override fun onDestroyView() {
        views = null
        super.onDestroyView()
    }

    private fun render(s: LogsState) {
        val v = views ?: return
        rendering = true
        s.device?.let { view?.subtitle(subtitle(it)) }
        v.kinds.check(if (s.source.kind == LogKind.KERNEL) R.id.kind_kernel else R.id.kind_system)
        v.bufferGroup.visibility = if (s.source.kind == LogKind.SYSTEM) View.VISIBLE else View.INVISIBLE
        for ((buffer, chip) in v.buffers) chip.isChecked = buffer in s.source.buffers
        for ((level, id) in LEVELS) {
            val on = level in s.filter.levels
            if (on != (id in v.levels.checkedButtonIds)) {
                if (on) v.levels.check(id) else v.levels.uncheck(id)
            }
        }
        v.live.chip(getString(if (s.paused) R.string.log_paused else R.string.log_live),
                if (s.paused) null else Level.GOOD)
        v.pause.setText(if (s.paused) R.string.log_resume else R.string.log_pause)
        rendering = false

        /* Kept at the newest line while the newest line is on show; left
         * where it is while someone reads further up. */
        val atEnd = !v.lines.canScrollVertically(1)
        v.adapter.submit(s.lines, s.filter.query)
        if (atEnd && s.lines.isNotEmpty()) v.lines.scrollToPosition(s.lines.size - 1)
    }

    private fun onEffect(effect: LogsEffect) {
        when (effect) {
            is LogsEffect.Share -> share(effect.zip)
            LogsEffect.Saved -> Toast.makeText(requireContext(), R.string.archive_saved, Toast.LENGTH_SHORT).show()
            LogsEffect.Failed -> Toast.makeText(requireContext(), R.string.archive_failed, Toast.LENGTH_SHORT).show()
        }
    }

    private fun share(zip: File) {
        val uri = FileProvider.getUriForFile(requireContext(), AUTHORITY, zip)
        val send = Intent(Intent.ACTION_SEND)
                .setType("application/zip")
                .putExtra(Intent.EXTRA_STREAM, uri)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        startActivity(Intent.createChooser(send, zip.name))
    }

    /* The build's date and the kernel's build number, which is what tells
     * one test image from the next: "20261027", "#109". */
    private fun subtitle(d: DeviceInfo): String {
        val build = Regex("\\.(\\d{8})\\.").find(d.build)?.groupValues?.get(1) ?: ""
        val kernel = Regex("#\\d+").find(d.kernel)?.value ?: ""
        return if (build.isEmpty() || kernel.isEmpty()) d.kernel else getString(R.string.logs_subtitle, build, kernel)
    }

    private companion object {
        const val AUTHORITY = "com.android.arttttt.mochamanager.files"
        val LEVELS = linkedMapOf(
                LogLevel.ERROR to R.id.level_error,
                LogLevel.WARN to R.id.level_warn,
                LogLevel.INFO to R.id.level_info,
                LogLevel.DEBUG to R.id.level_debug)
    }
}
