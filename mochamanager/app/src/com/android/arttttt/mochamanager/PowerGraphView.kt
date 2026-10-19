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
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.view.View

/* The last two minutes of the power drawn from the battery, a sample a
 * second, scaled to the highest of them. */
class PowerGraphView(context: Context) : View(context) {
    private val samples = IntArray(120)
    private var count = 0
    private var next = 0

    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2 * resources.displayMetrics.density
        color = context.getColor(android.R.color.holo_orange_light)
    }
    private val axis = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x40FFFFFF
        strokeWidth = resources.displayMetrics.density
    }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xB0FFFFFF.toInt()
        textSize = 12 * resources.displayMetrics.scaledDensity
    }
    private val path = Path()

    fun add(mw: Int) {
        samples[next] = mw
        next = (next + 1) % samples.size
        if (count < samples.size) count++
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val w = width.toFloat()
        val h = height.toFloat()
        canvas.drawLine(0f, h - 1, w, h - 1, axis)
        if (count < 2) return

        var top = 1000
        for (i in 0 until count) top = maxOf(top, samples[i])
        /* A round ceiling, so the scale does not twitch with every sample. */
        top = (top + 499) / 500 * 500

        path.reset()
        val first = (next - count + samples.size) % samples.size
        for (i in 0 until count) {
            val mw = samples[(first + i) % samples.size].coerceAtLeast(0)
            val x = w * i / (samples.size - 1)
            val y = h - h * mw / top
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        canvas.drawPath(path, line)
        canvas.drawText(Format.mw(top), 0f, label.textSize, label)
    }
}
