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
import android.graphics.Path
import android.util.AttributeSet
import android.view.View
import androidx.core.graphics.ColorUtils
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.usecase.History

/*
 * The last few seconds of one reading or of several, as lines with no axes:
 * how it moves, not what it is. A single line gets a soft fill under it;
 * several share one scale and are told apart by colour.
 */
class SparklineView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {
    private var series: List<History> = emptyList()
    private var colors: IntArray = intArrayOf()
    /* The scale's floor and ceiling; null to take them from the samples. */
    private var floor: Float? = null
    private var ceiling: Float? = null
    /* Each line about its own mean, all on one span no narrower than this:
     * how each moves, whatever its level. 0 for one scale of values. */
    private var centredSpan = 0f

    private val density = resources.displayMetrics.density
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.8f * density
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val path = Path()

    fun submit(series: List<History>, colors: IntArray, floor: Float? = null, ceiling: Float? = null) {
        this.series = series
        this.colors = colors
        this.floor = floor
        this.ceiling = ceiling
        centredSpan = 0f
        invalidate()
    }

    /* Several lines that sit at different levels -- an accelerometer's
     * axes, one bearing gravity -- each drawn about its own mean. */
    fun submitCentred(series: List<History>, colors: IntArray, minSpan: Float) {
        submit(series, colors)
        centredSpan = minSpan
    }

    /* One line in the accent. */
    fun submit(history: History, floor: Float? = null, ceiling: Float? = null) =
            submit(listOf(history), intArrayOf(themeColor(R.attr.colorPrimary)), floor, ceiling)

    override fun onDraw(canvas: Canvas) {
        val means = series.map { if (centredSpan > 0 && it.samples.isNotEmpty()) it.samples.average().toFloat() else 0f }
        val all = series.flatMapIndexed { i, h -> h.samples.map { it - means[i] } }
        if (all.size < 2) return
        var lo = floor ?: all.minOrNull()!!
        var hi = ceiling ?: all.maxOrNull()!!
        if (centredSpan > 0) {
            hi = maxOf(-lo, hi, centredSpan / 2)
            lo = -hi
        }
        if (hi - lo < 1e-6f) {
            lo -= 1f
            hi += 1f
        }
        val pad = stroke.strokeWidth
        val h = height - 2 * pad
        fun y(v: Float) = pad + h * (1 - (v - lo) / (hi - lo))

        for ((i, history) in series.withIndex()) {
            val samples = history.samples
            if (samples.size < 2) continue
            val step = width.toFloat() / (history.capacity - 1)
            /* The newest at the right edge, older ones to its left. */
            val start = width - step * (samples.size - 1)
            path.reset()
            for ((n, v) in samples.withIndex()) {
                val x = start + n * step
                if (n == 0) path.moveTo(x, y(v - means[i])) else path.lineTo(x, y(v - means[i]))
            }
            stroke.color = colors[i % colors.size]
            if (series.size == 1) {
                fill.color = ColorUtils.setAlphaComponent(stroke.color, 0x30)
                path.lineTo(width.toFloat(), height.toFloat())
                path.lineTo(start, height.toFloat())
                path.close()
                canvas.drawPath(path, fill)
                path.reset()
                for ((n, v) in samples.withIndex()) {
                    val x = start + n * step
                    if (n == 0) path.moveTo(x, y(v)) else path.lineTo(x, y(v))
                }
            }
            canvas.drawPath(path, stroke)
        }
    }
}
