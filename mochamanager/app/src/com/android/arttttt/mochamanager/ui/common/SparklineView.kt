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

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.View
import android.view.animation.DecelerateInterpolator
import android.view.animation.LinearInterpolator
import androidx.core.graphics.ColorUtils
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.usecase.History

/*
 * The last few seconds of one reading or of several, as lines with no axes:
 * how it moves, not what it is. A single line gets a soft fill under it;
 * several share one scale and are told apart by colour.
 *
 * It moves as the power graph does: a new sample comes in a step beyond the
 * right edge and the lines slide onto it over the time a sample lasts, so
 * they never stand still and never jump; a scale that has to change glides
 * to its new bounds.
 */
class SparklineView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {
    private var series: List<History> = emptyList()
    private var colors: IntArray = intArrayOf()
    /* The least the scale spans, from floor to ceiling, whatever the
     * samples; null to take it from them alone. A sample beyond either
     * widens the scale to it rather than being drawn outside. */
    private var floor: Float? = null
    private var ceiling: Float? = null
    /* Each line about its own mean, all on one span no narrower than this:
     * how each moves, whatever its level. 0 for one scale of values. */
    private var centredSpan = 0f
    private var means = FloatArray(0)

    /* How long a sample lasts: the slide takes that long. */
    var periodMs = 1000L

    /* 0 when a sample has just come in beyond the right edge, 1 once it has
     * slid onto it. */
    private var slide = 1f
    private val slider = ValueAnimator.ofFloat(0f, 1f).apply {
        interpolator = LinearInterpolator()
        addUpdateListener {
            slide = it.animatedValue as Float
            invalidate()
        }
    }

    private var lo = 0f
    private var hi = 1f
    private var targetLo = 0f
    private var targetHi = 1f
    private var scaler: ValueAnimator? = null
    private var shownSerial = -1L

    private val density = resources.displayMetrics.density
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.8f * density
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val path = Path()

    fun submit(series: List<History>, colors: IntArray, floor: Float? = null, ceiling: Float? = null) =
            take(series, colors, floor, ceiling, 0f)

    /* One line in the accent. */
    fun submit(history: History, floor: Float? = null, ceiling: Float? = null) =
            take(listOf(history), intArrayOf(themeColor(R.attr.colorPrimary)), floor, ceiling, 0f)

    /* Several lines that sit at different levels -- an accelerometer's
     * axes, one bearing gravity -- each drawn about its own mean. */
    fun submitCentred(series: List<History>, colors: IntArray, minSpan: Float) =
            take(series, colors, null, null, minSpan)

    private fun take(series: List<History>, colors: IntArray, floor: Float?, ceiling: Float?, span: Float) {
        val serial = series.firstOrNull()?.serial ?: -1L
        if (serial == shownSerial && floor == this.floor && ceiling == this.ceiling) return
        /* One sample more than drawn slides in; anything else -- the first,
         * a history kept across a rotation -- is drawn as it stands. */
        val next = shownSerial >= 0 && serial == shownSerial + 1
        this.series = series
        this.colors = colors
        this.floor = floor
        this.ceiling = ceiling
        centredSpan = span
        shownSerial = serial
        means = FloatArray(series.size) { i ->
            val s = series[i].samples
            if (span > 0 && s.isNotEmpty()) s.average().toFloat() else 0f
        }
        val (newLo, newHi) = range()
        if (!next) {
            slider.cancel()
            scaler?.cancel()
            slide = 1f
            lo = newLo
            hi = newHi
            targetLo = newLo
            targetHi = newHi
            invalidate()
            return
        }
        slider.duration = periodMs
        slider.start()
        if (newLo != targetLo || newHi != targetHi) glideTo(newLo, newHi)
    }

    /* The bounds the samples need, with room above and below. */
    private fun range(): Pair<Float, Float> {
        val all = series.flatMapIndexed { i, h -> h.samples.map { it - means[i] } }
        if (all.isEmpty()) return 0f to 1f
        var lo = minOf(floor ?: Float.MAX_VALUE, all.minOrNull()!!)
        var hi = maxOf(ceiling ?: -Float.MAX_VALUE, all.maxOrNull()!!)
        if (centredSpan > 0) {
            hi = maxOf(-lo, hi, centredSpan / 2)
            lo = -hi
        }
        if (hi - lo < 1e-6f) {
            lo -= 1f
            hi += 1f
        }
        return lo to hi
    }

    /* From wherever the scale is now, even halfway through a glide. */
    private fun glideTo(newLo: Float, newHi: Float) {
        targetLo = newLo
        targetHi = newHi
        val fromLo = lo
        val fromHi = hi
        scaler?.cancel()
        scaler = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = maxOf(periodMs, 300L)
            interpolator = DecelerateInterpolator()
            addUpdateListener {
                val f = it.animatedValue as Float
                lo = fromLo + (newLo - fromLo) * f
                hi = fromHi + (newHi - fromHi) * f
                invalidate()
            }
            start()
        }
    }

    override fun onDetachedFromWindow() {
        slider.cancel()
        scaler?.cancel()
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        val pad = stroke.strokeWidth
        val h = height - 2 * pad
        fun y(v: Float) = pad + h * (1 - (v - lo) / (hi - lo))

        canvas.save()
        canvas.clipRect(0f, 0f, width.toFloat(), height.toFloat())
        for ((i, history) in series.withIndex()) {
            val samples = history.samples
            if (samples.size < 2) continue
            val step = width.toFloat() / (history.capacity - 1)
            /* The newest a step beyond the edge, sliding onto it. */
            val edge = width + step * (1 - slide)
            fun x(n: Int) = edge - (samples.size - 1 - n) * step
            path.reset()
            for ((n, v) in samples.withIndex()) {
                if (n == 0) path.moveTo(x(n), y(v - means[i])) else path.lineTo(x(n), y(v - means[i]))
            }
            stroke.color = colors[i % colors.size]
            if (series.size == 1) {
                fill.color = ColorUtils.setAlphaComponent(stroke.color, 0x30)
                canvas.drawPath(path, stroke)
                path.lineTo(x(samples.size - 1), height.toFloat())
                path.lineTo(x(0), height.toFloat())
                path.close()
                canvas.drawPath(path, fill)
            } else {
                canvas.drawPath(path, stroke)
            }
        }
        canvas.restore()
    }
}
