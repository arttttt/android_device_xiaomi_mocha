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

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Loadable

/* A value as the screens show it: an ellipsis while it is on its way, a dash
 * when it is not to be had. */
fun <T> Fragment.text(value: Loadable<T>, show: (T) -> CharSequence): CharSequence = when (value) {
    is Loadable.Ready -> show(value.value)
    Loadable.Loading -> getString(R.string.loading)
    Loadable.Unavailable -> getString(R.string.unknown)
}

/* A label-and-value row added to a card; its value view to set. */
fun ViewGroup.addRow(label: CharSequence): TextView {
    val row = LayoutInflater.from(context).inflate(R.layout.item_row, this, false)
    row.findViewById<TextView>(R.id.row_label).text = label
    addView(row)
    return row.findViewById(R.id.row_value)
}

/* A screen's title and the word on what it is about. */
fun View.header(title: Int, subtitle: CharSequence = "") {
    findViewById<TextView>(R.id.screen_title).setText(title)
    findViewById<TextView>(R.id.screen_subtitle).text = subtitle
}

fun View.subtitle(text: CharSequence) {
    findViewById<TextView>(R.id.screen_subtitle).text = text
}

/* A card's heading, included as card_title under the given id. */
class CardTitle(root: View) {
    private val title: TextView = root.findViewById(R.id.card_title)
    val aside: TextView = root.findViewById(R.id.card_aside)

    fun set(text: Int) = title.setText(text)
    fun set(text: CharSequence) {
        title.text = text
    }
}

fun View.cardTitle(id: Int, text: Int) = CardTitle(findViewById(id)).apply { set(text) }
