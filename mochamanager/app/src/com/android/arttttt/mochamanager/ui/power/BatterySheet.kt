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

package com.android.arttttt.mochamanager.ui.power

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.os.bundleOf
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment

/*
 * Whether to run on the battery with the cable in, asked over the screen
 * whose controls chose it. A yes is the screen's to act on: it comes back as
 * a fragment result.
 */
class BatterySheet : BottomSheetDialogFragment() {
    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?): View =
            inflater.inflate(R.layout.sheet_battery, container, false)

    override fun onViewCreated(view: View, saved: Bundle?) {
        (dialog as? BottomSheetDialog)?.behavior?.apply {
            state = BottomSheetBehavior.STATE_EXPANDED
            skipCollapsed = true
        }
        view.findViewById<View>(R.id.cancel).setOnClickListener { dismiss() }
        view.findViewById<View>(R.id.enable).setOnClickListener {
            parentFragmentManager.setFragmentResult(RESULT, bundleOf())
            dismiss()
        }
    }

    companion object {
        private const val TAG = "battery"
        private const val RESULT = "battery"

        /* Asks over host, and calls yes from it once the answer is yes. */
        fun ask(host: Fragment) {
            if (host.childFragmentManager.findFragmentByTag(TAG) == null) {
                BatterySheet().show(host.childFragmentManager, TAG)
            }
        }

        fun onYes(host: Fragment, yes: () -> Unit) {
            host.childFragmentManager.setFragmentResultListener(RESULT, host.viewLifecycleOwner) { _, _ -> yes() }
        }
    }
}
