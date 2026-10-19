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
    private val capacity = 120
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
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2 * density
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
        color = context.getColor(android.R.color.holo_orange_light)
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val axis = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x30FFFFFF
        strokeWidth = density
    }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xB0FFFFFF.toInt()
        textSize = 12 * resources.displayMetrics.scaledDensity
    }
    private val curve = Path()
    private val area = Path()

    /* How long a sample lasts on screen before the next: the slide takes
     * that long, so the curve never stops between samples. */
    var periodMs = 1000L

    fun add(mw: Int) {
        newest = (newest + 1) % capacity
        samples[newest] = mw.coerceAtLeast(0).toFloat()
        if (count < capacity) count++

        slider.duration = periodMs
        slider.start()

        var peak = 0f
        for (i in 0 until count) peak = maxOf(peak, samples[i])
        /* A round ceiling with headroom, so the scale moves seldom. */
        val target = maxOf(1000f, Math.ceil((peak * 1.15f) / 500.0).toFloat() * 500f)
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
        fill.shader = LinearGradient(0f, 0f, 0f, h.toFloat(),
                0x60FFBB33, 0x00FFBB33, Shader.TileMode.CLAMP)
    }

    override fun onDetachedFromWindow() {
        slider.cancel()
        scaler?.cancel()
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        val w = width.toFloat()
        val h = height.toFloat()
        canvas.drawLine(0f, h - 1, w, h - 1, axis)
        canvas.drawLine(0f, h / 2, w, h / 2, axis)
        canvas.drawText(Format.mw(top.toInt()), 0f, label.textSize, label)
        if (count < 2) return

        val step = w / (capacity - 2)
        /* The newest sample sits a step beyond the edge and slides onto it. */
        val right = w + step * (1 - slide)
        fun x(age: Int) = right - age * step
        fun y(age: Int) = h - h * samples[(newest - age + capacity) % capacity] / top

        canvas.save()
        canvas.clipRect(0f, 0f, w, h)
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
        area.lineTo(x(0), h)
        area.lineTo(x(oldest), h)
        area.close()
        canvas.drawPath(area, fill)
        canvas.drawPath(curve, stroke)
        canvas.restore()
    }
}
