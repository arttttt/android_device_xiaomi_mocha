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
import android.view.animation.DecelerateInterpolator
import android.view.animation.LinearInterpolator
import android.widget.TextView
import androidx.fragment.app.Fragment
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.model.Loadable

/*
 * Numbers that run to a new value rather than jump to it. A TextView keeps
 * the numbers it shows in a tag; a new value starts a run from wherever it
 * stands -- even halfway through the last run -- and each frame formats
 * the numbers in between. A run as long as the time to the next value
 * keeps a stream of readings moving without a pause: such short runs go at
 * an even pace, longer ones ease out.
 */

/* Long enough to see, shorter than the shortest poll. */
const val NUMBER_MS = 500L

/* As often as the sensors screen draws: one run per reading. */
const val STREAM_MS = 200L

private class Rolling(var shown: DoubleArray, var show: (DoubleArray) -> CharSequence) {
    var animator: ValueAnimator? = null
}

/*
 * values, shown by show. With wrap -- 360 for angles -- each runs the short
 * way round, so show must bring them back into range.
 */
fun TextView.roll(
    values: DoubleArray,
    durationMs: Long = NUMBER_MS,
    wrap: Double = 0.0,
    show: (DoubleArray) -> CharSequence,
) {
    val r = getTag(R.id.rolling) as Rolling?
    if (r == null || r.shown.size != values.size) {
        r?.animator?.cancel()
        setTag(R.id.rolling, Rolling(values.copyOf(), show))
        text = show(values)
        return
    }
    r.show = show
    val from = r.shown.copyOf()
    val to = DoubleArray(values.size) { i ->
        if (wrap > 0) from[i] + shortest(values[i] - from[i], wrap) else values[i]
    }
    r.animator?.cancel()
    if (from.contentEquals(to)) {
        text = show(from)
        return
    }
    r.animator = ValueAnimator.ofFloat(0f, 1f).apply {
        duration = durationMs
        interpolator = if (durationMs <= STREAM_MS) LinearInterpolator() else DecelerateInterpolator()
        addUpdateListener { a ->
            val f = a.animatedValue as Float
            for (i in from.indices) r.shown[i] = from[i] + (to[i] - from[i]) * f
            text = r.show(r.shown)
        }
        start()
    }
}

fun TextView.roll(value: Double, durationMs: Long = NUMBER_MS, wrap: Double = 0.0, show: (Double) -> CharSequence) =
        roll(doubleArrayOf(value), durationMs, wrap) { show(it[0]) }

/* Text that is no number, or none yet: the next number starts from itself. */
fun TextView.settle(text: CharSequence) {
    (getTag(R.id.rolling) as Rolling?)?.animator?.cancel()
    setTag(R.id.rolling, null)
    this.text = text
}

/* A value still on its way, or not to be had, as text; once there, rolling. */
fun <T> Fragment.roll(view: TextView, value: Loadable<T>, number: (T) -> Double, show: (Double) -> CharSequence) {
    val v = value.valueOrNull
    if (v == null) view.settle(text(value) { "" }) else view.roll(number(v), show = show)
}

fun <T> Fragment.rollAll(
    view: TextView,
    value: Loadable<T>,
    numbers: (T) -> DoubleArray,
    show: (DoubleArray) -> CharSequence,
) {
    val v = value.valueOrNull
    if (v == null) view.settle(text(value) { "" }) else view.roll(numbers(v), show = show)
}

/* The angle into -180..180, the shorter way from the one before. */
fun shortest(delta: Double, wrap: Double): Double {
    val d = ((delta % wrap) + wrap) % wrap
    return if (d > wrap / 2) d - wrap else d
}
