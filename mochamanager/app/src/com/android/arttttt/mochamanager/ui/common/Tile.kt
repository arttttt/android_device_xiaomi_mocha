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

package com.android.arttttt.mochamanager.ui.common

import android.view.View
import android.widget.TextView
import com.android.arttttt.mochamanager.R
import com.google.android.material.card.MaterialCardView
import com.google.android.material.progressindicator.LinearProgressIndicator

/* An item_tile, included under the given id. A tile that opens a section
 * says so with a chevron. */
class Tile(parent: View, id: Int, title: Int, onOpen: (() -> Unit)? = null) {
    private val card: MaterialCardView = parent.findViewById(id)
    val value: TextView = card.findViewById(R.id.tile_value)
    val sub: TextView = card.findViewById(R.id.tile_sub)
    val bar: LinearProgressIndicator = card.findViewById(R.id.tile_bar)
    val bars: BarsView = card.findViewById(R.id.tile_bars)

    init {
        val heading = CardTitle(card.findViewById(R.id.tile_title))
        heading.set(title)
        if (onOpen != null) {
            heading.aside.setText(R.string.open)
            heading.aside.visibility = View.VISIBLE
            heading.aside.background = null
            card.setOnClickListener { onOpen() }
        }
    }
}
