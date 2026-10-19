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

import android.content.res.ColorStateList
import android.view.View
import android.widget.TextView
import com.android.arttttt.mochamanager.R
import com.android.arttttt.mochamanager.domain.usecase.Level
import com.google.android.material.color.MaterialColors
import com.google.android.material.progressindicator.LinearProgressIndicator

/* The colours a state is drawn in, from the theme, so light and dark each
 * have their own. null is no state: the neutral ones. */

fun View.themeColor(attr: Int) = MaterialColors.getColor(this, attr)

fun View.levelColor(level: Level?) = themeColor(when (level) {
    Level.GOOD -> R.attr.colorGood
    Level.WARN -> R.attr.colorWarn
    Level.BAD -> R.attr.colorBad
    null -> R.attr.colorOnSurface
})

private fun View.levelContainer(level: Level?) = themeColor(when (level) {
    Level.GOOD -> R.attr.colorGoodContainer
    Level.WARN -> R.attr.colorWarnContainer
    Level.BAD -> R.attr.colorBadContainer
    null -> R.attr.colorSurfaceVariant
})

/* A chip: the state's soft container behind its colour. */
fun TextView.chip(text: CharSequence, level: Level?) {
    this.text = text
    visibility = View.VISIBLE
    backgroundTintList = ColorStateList.valueOf(levelContainer(level))
    setTextColor(if (level == null) themeColor(R.attr.colorOnSurfaceVariant) else levelColor(level))
}

/* A bar filled to fraction, 0..1, in the state's colour or the accent. */
fun LinearProgressIndicator.fill(fraction: Double, level: Level? = null) {
    setIndicatorColor(if (level == null) themeColor(R.attr.colorPrimary) else levelColor(level))
    setProgressCompat((fraction.coerceIn(0.0, 1.0) * max).toInt(), true)
}
