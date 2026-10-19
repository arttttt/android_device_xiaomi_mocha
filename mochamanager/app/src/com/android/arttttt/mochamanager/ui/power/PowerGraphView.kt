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

import com.android.arttttt.mochamanager.R
import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Shader
import android.view.View
import android.view.animation.DecelerateInterpolator
import android.view.animation.LinearInterpolator
import com.android.arttttt.mochamanager.domain.usecase.PowerHistory

/*
 * The last two minutes of the power drawn from the battery, a sample a
 * second, drawn as it moves.
 *
 * Two animations, each driven by its own animator's value rather than by
 * redrawing on every sample: the curve slides left by one step over the
 * second a sample lasts, the new sample coming in at the right edge, and the
 * scale glides to the new ceiling instead of jumping to it.
 */
class PowerGraphView(context: Context) : View(context) {
    private val capacity = PowerHistory.CAPACITY
    private val samples = FloatArray(capacity)
    private var count = 0
    private var newest = -1

    /* 0 when a sample has just come in beyond the right edge, 1 once it has
     * slid onto it. */
    private var slide = 1f
    private var top = 1000f
    private var targetTop = 1000f

    private val slider = ValueAnimator.ofFloat(0f, 1f).apply {
        interpolator = LinearInterpolator()
        addUpdateListener {
            slide = it.animatedValue as Float
            invalidate()
        }
    }
    private var scaler: ValueAnimator? = null

    private val density = resources.displayMetrics.density
    private val scaled = resources.displayMetrics.scaledDensity
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2 * density
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
        color = context.getColor(android.R.color.holo_orange_light)
    }
    private val dot = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = stroke.color }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val grid = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x30FFFFFF
        strokeWidth = density
    }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xA0FFFFFF.toInt()
        textSize = 11 * scaled
    }
    private val value = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = stroke.color
        textSize = 13 * scaled
        textAlign = Paint.Align.RIGHT
        isFakeBoldText = true
    }
    private val curve = Path()
    private val area = Path()

    private val title = context.getString(R.string.graph_title)
    private val now = context.getString(R.string.graph_now)
    private val oneMinute = context.getString(R.string.graph_minutes, 1)
    private val twoMinutes = context.getString(R.string.graph_minutes, 2)

    /* The plot inside the labels: the scale to its left, the time below,
     * the title above. */
    private val plotLeft = 40 * density
    private val plotTop = 22 * scaled
    private val plotBottomGap = 18 * scaled

    /* How long a sample lasts on screen before the next: the slide takes
     * that long, so the curve never stops between samples. */
    var periodMs = 1000L

    private var shownSerial = -1L

    /*
     * The history as the model has it. One sample more than drawn slides in;
     * anything else -- the first, a history kept across a rotation -- is
     * drawn as it stands, without a slide.
     */
    fun submit(history: PowerHistory) {
        if (history.serial == shownSerial) return
        val samplesMw = history.samplesMw
        if (shownSerial >= 0 && history.serial == shownSerial + 1 && samplesMw.isNotEmpty()) {
            add(samplesMw.last())
        } else {
            count = samplesMw.size
            for ((i, mw) in samplesMw.withIndex()) samples[i] = mw.coerceAtLeast(0).toFloat()
            newest = count - 1
            slide = 1f
            top = ceiling()
            targetTop = top
            invalidate()
        }
        shownSerial = history.serial
    }

    /* A round ceiling with headroom, so the scale moves seldom. */
    private fun ceiling(): Float {
        var peak = 0f
        for (i in 0 until count) peak = maxOf(peak, samples[i])
        return maxOf(1000f, Math.ceil((peak * 1.15f) / 500.0).toFloat() * 500f)
    }

    private fun add(mw: Int) {
        newest = (newest + 1) % capacity
        samples[newest] = mw.coerceAtLeast(0).toFloat()
        if (count < capacity) count++

        slider.duration = periodMs
        slider.start()

        val target = ceiling()
        if (target != targetTop) glideTo(target)
    }

    /* From wherever the scale is now, even halfway through a glide. */
    private fun glideTo(target: Float) {
        targetTop = target
        scaler?.cancel()
        scaler = ValueAnimator.ofFloat(top, target).apply {
            duration = 600
            interpolator = DecelerateInterpolator()
            addUpdateListener {
                top = it.animatedValue as Float
                invalidate()
            }
            start()
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        fill.shader = LinearGradient(0f, plotTop, 0f, h - plotBottomGap,
                0x60FFBB33, 0x00FFBB33, Shader.TileMode.CLAMP)
    }

    override fun onDetachedFromWindow() {
        slider.cancel()
        scaler?.cancel()
        super.onDetachedFromWindow()
    }

    /* Watts with no more digits than the value needs: 2.5, 1.25, 0. */
    private fun watts(mw: Float): String {
        val s = String.format(java.util.Locale.getDefault(), "%.2f", mw / 1000f)
        return s.trimEnd('0').trimEnd('.', ',')
    }

    override fun onDraw(canvas: Canvas) {
        val left = plotLeft
        val right = width.toFloat()
        val top = plotTop
        val bottom = height - plotBottomGap
        val plotH = bottom - top

        canvas.drawText(title, 0f, 13 * scaled, label)

        /* The scale: the floor, the middle and the ceiling. */
        for ((fraction, mw) in listOf(0f to 0f, 0.5f to this.top / 2, 1f to this.top)) {
            val y = bottom - plotH * fraction
            canvas.drawLine(left, y, right, y, grid)
            canvas.drawText(watts(mw), 0f, y + label.textSize / 3, label)
        }

        /* The time: a sample a second, so the plot spans two minutes. */
        val labelY = height - 4 * density
        label.textAlign = Paint.Align.LEFT
        canvas.drawText(twoMinutes, left, labelY, label)
        label.textAlign = Paint.Align.CENTER
        canvas.drawText(oneMinute, (left + right) / 2, labelY, label)
        label.textAlign = Paint.Align.RIGHT
        canvas.drawText(now, right, labelY, label)
        label.textAlign = Paint.Align.LEFT

        if (count < 2) return

        val step = (right - left) / (capacity - 2)
        /* The newest sample sits a step beyond the edge and slides onto it. */
        val edge = right + step * (1 - slide)
        fun x(age: Int) = edge - age * step
        fun y(age: Int) = bottom - plotH * samples[(newest - age + capacity) % capacity] / this.top

        canvas.save()
        canvas.clipRect(left, 0f, right, bottom)
        curve.reset()
        val oldest = count - 1
        curve.moveTo(x(oldest), y(oldest))
        /* Through the midpoints, each sample a control point: a smooth line
         * that never overshoots the readings it passes between. */
        for (age in oldest - 1 downTo 1) {
            val mx = (x(age) + x(age - 1)) / 2
            val my = (y(age) + y(age - 1)) / 2
            curve.quadTo(x(age), y(age), mx, my)
        }
        curve.lineTo(x(0), y(0))

        area.set(curve)
        area.lineTo(x(0), bottom)
        area.lineTo(x(oldest), bottom)
        area.close()
        canvas.drawPath(area, fill)
        canvas.drawPath(curve, stroke)
        canvas.restore()

        /* The reading now, where the line meets the edge: the newest sample
         * eased in from the one before, as the slide brings it on. */
        val previous = samples[(newest - 1 + capacity) % capacity]
        val current = previous + (samples[newest] - previous) * slide
        val cy = bottom - plotH * current / this.top
        canvas.drawCircle(right - 3 * density, cy, 3 * density, dot)
        canvas.drawText(watts(current), right - 10 * density,
                (cy - 8 * density).coerceAtLeast(top + value.textSize), value)
    }
}
