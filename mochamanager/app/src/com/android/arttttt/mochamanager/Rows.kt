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

import android.content.Context
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/*
 * A screen as sections of label-and-value rows, built in code: the screens
 * are lists of facts, and a layout file per list would only repeat itself.
 * Rows are kept by a key so a refresh sets their values in place.
 */
class Rows(private val context: Context) {
    val root = ScrollView(context)
    private val column = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        val pad = dp(16)
        setPadding(pad, pad / 2, pad, pad)
    }
    private val values = HashMap<String, TextView>()

    init {
        root.addView(column)
    }

    fun section(title: Int) {
        column.addView(TextView(context).apply {
            setText(title)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
            setTextColor(context.getColor(android.R.color.holo_blue_light))
            setPadding(0, dp(16), 0, dp(4))
        })
    }

    fun row(key: String, label: CharSequence) {
        val line = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(6), 0, dp(6))
        }
        line.addView(TextView(context).apply {
            text = label
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
        }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        val value = TextView(context).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            gravity = Gravity.END
            setTextIsSelectable(true)
        }
        line.addView(value, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.4f))
        column.addView(line)
        values[key] = value
    }

    fun row(key: String, label: Int) = row(key, context.getString(label))

    fun note(text: Int) = note(context.getString(text))

    fun note(text: CharSequence) {
        column.addView(TextView(context).apply {
            this.text = text
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            alpha = 0.7f
            setPadding(0, dp(2), 0, dp(6))
        })
    }

    fun add(view: View) = column.addView(view)

    fun has(key: String) = values.containsKey(key)

    operator fun set(key: String, value: CharSequence) {
        values[key]?.text = value
    }

    fun dp(value: Int) = (value * context.resources.displayMetrics.density).toInt()
}
