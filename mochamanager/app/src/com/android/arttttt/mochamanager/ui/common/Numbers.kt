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
import android.text.TextUtils
import android.view.View
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
 *
 * Cheap per frame, as it must be for a screen of them: given the step its
 * text shows -- 0.01 for two decimals -- a frame whose numbers round to the
 * last one's formats nothing; a frame whose text comes out as the last
 * one's sets nothing; and a TextView of a fixed or
 * matched width -- as the screens give every running number -- only
 * redraws itself on a new text rather than laying the screen out anew.
 */

/* Long enough to see, shorter than the shortest poll. */
const val NUMBER_MS = 500L

/* As often as the sensors screen draws: one run per reading. */
const val STREAM_MS = 200L

/*
 * A view's run lasts no longer than the view is on screen: once it leaves
 * the window -- the screen destroyed on a rotation -- the run is called off
 * and forgotten, so no animator keeps the old views, and the screen whose
 * text they format, alive, nor formats with a screen no longer attached.
 */
private class Rolling(
    val view: TextView,
    var shown: DoubleArray,
    var show: (DoubleArray) -> CharSequence,
) : View.OnAttachStateChangeListener {
    var animator: ValueAnimator? = null
    var step = 0.0
    /* The numbers as last formatted, in steps. */
    private var formatted: LongArray? = null

    /* Formats only numbers that show differently from the last. */
    fun showIfChanged() {
        if (step > 0) {
            val steps = LongArray(shown.size) { Math.round(shown[it] / step) }
            if (steps.contentEquals(formatted)) return
            formatted = steps
        }
        view.setIfNew(show(shown))
    }

    init {
        view.addOnAttachStateChangeListener(this)
    }

    override fun onViewAttachedToWindow(v: View) {}

    override fun onViewDetachedFromWindow(v: View) = forget()

    fun forget() {
        animator?.cancel()
        animator = null
        view.removeOnAttachStateChangeListener(this)
        view.setTag(R.id.rolling, null)
    }
}

/*
 * values, shown by show. With wrap -- 360 for angles -- each runs the short
 * way round, so show must bring them back into range.
 */
fun TextView.roll(
    values: DoubleArray,
    durationMs: Long = NUMBER_MS,
    wrap: Double = 0.0,
    step: Double = 0.0,
    show: (DoubleArray) -> CharSequence,
) {
    val r = getTag(R.id.rolling) as Rolling?
    if (r == null || r.shown.size != values.size || !isAttachedToWindow) {
        r?.forget()
        text = show(values)
        if (isAttachedToWindow) setTag(R.id.rolling, Rolling(this, values.copyOf(), show).also { it.step = step })
        return
    }
    r.show = show
    r.step = step
    val from = r.shown.copyOf()
    val to = DoubleArray(values.size) { i ->
        if (wrap > 0) from[i] + shortest(values[i] - from[i], wrap) else values[i]
    }
    r.animator?.cancel()
    if (from.contentEquals(to)) {
        setIfNew(show(from))
        return
    }
    r.animator = ValueAnimator.ofFloat(0f, 1f).apply {
        duration = durationMs
        interpolator = if (durationMs <= STREAM_MS) LinearInterpolator() else DecelerateInterpolator()
        addUpdateListener { a ->
            val f = a.animatedValue as Float
            for (i in from.indices) r.shown[i] = from[i] + (to[i] - from[i]) * f
            r.showIfChanged()
        }
        start()
    }
}

fun TextView.roll(
    value: Double,
    durationMs: Long = NUMBER_MS,
    wrap: Double = 0.0,
    step: Double = 0.0,
    show: (Double) -> CharSequence,
) = roll(doubleArrayOf(value), durationMs, wrap, step) { show(it[0]) }

internal fun TextView.setIfNew(new: CharSequence) {
    if (!TextUtils.equals(text, new)) text = new
}

/* Text that is no number, or none yet: the next number starts from itself. */
fun TextView.settle(text: CharSequence) {
    (getTag(R.id.rolling) as Rolling?)?.forget()
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
