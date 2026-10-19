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
 * The range SurfaceFlinger may choose the refresh rate from, as the
 * framework's min and peak refresh rate settings give it; 0 for no lower
 * bound. The composer offers 60 and 30 Hz as one seamless group and drops to
 * 30 after a while with no new frame.
 */
enum class RefreshMode(val min: Float, val peak: Float) {
    AUTO(0f, 60f),
    FIXED_60(60f, 60f),
    FIXED_30(0f, 30f);

    companion object {
        /* null for a range none of these is. */
        fun of(min: Float, peak: Float): RefreshMode? =
                values().firstOrNull { it.min == min && it.peak == peak }
    }
}
