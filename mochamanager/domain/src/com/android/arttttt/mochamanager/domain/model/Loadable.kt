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

package com.android.arttttt.mochamanager.domain.model

/*
 * A value that has to be fetched: not there yet, there, or not to be had --
 * the service did not answer. A screen draws each of the three, so none of
 * them is an empty screen.
 */
sealed class Loadable<out T> {
    object Loading : Loadable<Nothing>()
    object Unavailable : Loadable<Nothing>()
    data class Ready<T>(val value: T) : Loadable<T>()

    val valueOrNull: T?
        get() = (this as? Ready<T>)?.value

    fun <R> map(transform: (T) -> R): Loadable<R> = when (this) {
        is Ready -> Ready(transform(value))
        Loading -> Loading
        Unavailable -> Unavailable
    }

    companion object {
        fun <T> of(value: T?): Loadable<T> = if (value == null) Unavailable else Ready(value)
    }
}
