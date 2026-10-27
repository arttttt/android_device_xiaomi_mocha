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

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.CompoundButton
import android.widget.TextView
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.usecase.ArchivePart
import com.android.arttttt.mochamanager.ui.common.collectWhileShown
import com.android.arttttt.mochamanager.ui.common.screenViewModel
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment

/*
 * What goes in the log archive, and whether it is shared or kept in
 * Downloads. It shows the logs screen's model, which makes the archive and
 * hands it on; the sheet goes once it is made.
 */
class ArchiveSheet : BottomSheetDialogFragment() {
    private val model by screenViewModel({ requireParentFragment() }, LogsViewModel::create)
    private val checks = LinkedHashMap<ArchivePart, CompoundButton>()
    private var started = false

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View =
            inflater.inflate(R.layout.sheet_archive, container, false)

    override fun onViewCreated(view: View, saved: Bundle?) {
        (dialog as? BottomSheetDialog)?.behavior?.apply {
            state = BottomSheetBehavior.STATE_EXPANDED
            skipCollapsed = true
        }
        val parts: ViewGroup = view.findViewById(R.id.parts)
        checks.clear()
        for ((part, title, sub) in PARTS) {
            val row = layoutInflater.inflate(R.layout.item_archive_part, parts, false)
            val check: CompoundButton = row.findViewById(R.id.part_check)
            check.setText(title)
            row.findViewById<TextView>(R.id.part_sub).text = if (sub == 0) "" else getString(sub)
            parts.addView(row)
            checks[part] = check
        }
        val redact: CompoundButton = view.findViewById(R.id.redact)
        val progress: View = view.findViewById(R.id.progress)
        val buttons = listOf<View>(view.findViewById(R.id.downloads), view.findViewById(R.id.share))
        view.findViewById<View>(R.id.downloads).setOnClickListener { export(redact.isChecked, ArchiveTarget.DOWNLOADS) }
        view.findViewById<View>(R.id.share).setOnClickListener { export(redact.isChecked, ArchiveTarget.SHARE) }

        collectWhileShown(model.state) { s ->
            progress.visibility = if (s.collecting) View.VISIBLE else View.GONE
            for (b in buttons) b.isEnabled = !s.collecting
            if (started && !s.collecting) dismiss()
        }
    }

    private fun export(redact: Boolean, to: ArchiveTarget) {
        val parts = checks.filterValues { it.isChecked }.keys
        if (parts.isEmpty()) return
        started = true
        model.onAction(LogsAction.Export(parts, redact, to))
    }

    companion object {
        private const val TAG = "archive"

        private val PARTS = listOf(
                Triple(ArchivePart.SYSTEM_LOG, R.string.archive_system, 0),
                Triple(ArchivePart.KERNEL_LOG, R.string.archive_kernel, 0),
                Triple(ArchivePart.LAST_KERNEL_LOG, R.string.archive_last, R.string.archive_last_sub),
                Triple(ArchivePart.CRASHES, R.string.archive_crashes, R.string.archive_crashes_sub),
                Triple(ArchivePart.DEVICE, R.string.archive_device, R.string.archive_device_sub))

        fun ask(host: Fragment) {
            if (host.childFragmentManager.findFragmentByTag(TAG) == null) {
                ArchiveSheet().show(host.childFragmentManager, TAG)
            }
        }
    }
}
