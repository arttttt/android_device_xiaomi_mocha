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

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.Fragment

/*
 * A tab: builds its rows once, then refreshes them every period while it is
 * on screen and not otherwise -- the readings cost a sysfs read each, and the
 * INA a conversion. A period of 0 refreshes once.
 */
abstract class Screen(private val periodMs: Long) : Fragment() {
    protected lateinit var rows: Rows
    private val handler = Handler(Looper.getMainLooper())
    private var running = false
    private var warned = false

    private val tick = object : Runnable {
        override fun run() {
            refresh()
            if (running && periodMs > 0) handler.postDelayed(this, periodMs)
        }
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?,
            savedInstanceState: Bundle?): View {
        rows = Rows(requireContext())
        build()
        return rows.root
    }

    override fun onResume() {
        super.onResume()
        running = true
        handler.post(tick)
    }

    override fun onPause() {
        running = false
        handler.removeCallbacks(tick)
        super.onPause()
    }

    protected abstract fun build()
    protected abstract fun refresh()

    /* Once per screen: the service will be asked again on the next tick. */
    protected fun unavailable() {
        if (warned || !isAdded) return
        warned = true
        Toast.makeText(requireContext(), R.string.service_unavailable, Toast.LENGTH_SHORT).show()
    }
}
