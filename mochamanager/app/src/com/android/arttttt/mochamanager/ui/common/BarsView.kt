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

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import com.android.arttttt.mochamanager.R

/* A few upright bars side by side, each filled 0..1: the cores at a glance. */
class BarsView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {
    private var values = FloatArray(0)
    private val density = resources.displayMetrics.density
    private val track = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = themeColor(R.attr.colorTrack) }
    private val bar = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = themeColor(R.attr.colorPrimary) }
    private val rect = RectF()

    fun submit(values: FloatArray) {
        if (values.contentEquals(this.values)) return
        this.values = values
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        if (values.isEmpty()) return
        val gap = 6 * density
        val w = (width - gap * (values.size - 1)) / values.size
        val r = 3 * density
        for ((i, v) in values.withIndex()) {
            val left = i * (w + gap)
            rect.set(left, 0f, left + w, height.toFloat())
            canvas.drawRoundRect(rect, r, r, track)
            /* Never quite empty: a core at its floor still shows it runs. */
            rect.top = height * (1 - v.coerceIn(0.06f, 1f))
            canvas.drawRoundRect(rect, r, r, bar)
        }
    }
}
