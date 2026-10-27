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

import android.annotation.SuppressLint
import android.text.SpannableString
import android.text.Spanned
import android.text.style.BackgroundColorSpan
import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.LogLevel
import com.android.arttttt.mochamanager.domain.model.LogLine
import com.android.arttttt.mochamanager.ui.common.themeColor

/* A log's lines, coloured by level, what is searched for marked in them. */
class LogAdapter : RecyclerView.Adapter<LogAdapter.Holder>() {
    class Holder(val text: TextView) : RecyclerView.ViewHolder(text)

    private var lines: List<LogLine> = emptyList()
    private var query = ""

    /* The whole list at once: a log only grows at its end and drops its
     * start, and a diff of thousands of lines four times a second costs more
     * than drawing the few on screen again. */
    @SuppressLint("NotifyDataSetChanged")
    fun submit(lines: List<LogLine>, query: String) {
        if (lines === this.lines && query == this.query) return
        this.lines = lines
        this.query = query
        notifyDataSetChanged()
    }

    override fun getItemCount() = lines.size

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) =
            Holder(LayoutInflater.from(parent.context).inflate(R.layout.item_log_line, parent, false) as TextView)

    override fun onBindViewHolder(holder: Holder, position: Int) {
        val line = lines[position]
        val view = holder.text
        view.setTextColor(view.themeColor(when (line.level) {
            LogLevel.ERROR -> R.attr.colorBad
            LogLevel.WARN -> R.attr.colorWarn
            LogLevel.DEBUG -> R.attr.colorOnSurfaceVariant
            else -> R.attr.colorOnSurface
        }))
        view.text = if (query.isEmpty()) line.text else marked(line.text, view.themeColor(R.attr.colorPrimaryContainer))
    }

    private fun marked(text: String, color: Int): CharSequence {
        val out = SpannableString(text)
        var from = text.indexOf(query, ignoreCase = true)
        while (from >= 0) {
            out.setSpan(BackgroundColorSpan(color), from, from + query.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            from = text.indexOf(query, from + query.length, ignoreCase = true)
        }
        return out
    }
}
