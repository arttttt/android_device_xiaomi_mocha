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

/* What the tablet is. */
data class DeviceInfo(
    val model: String,
    val build: String,
    val kernel: String,
    val wifi: String,
    val memTotalKb: Long,
    /* null when it may not be read. */
    val serial: String?,
) {
    /* The 3 GB model is the one with more than 2 GiB for Linux, as the heap
     * settings used to tell them apart. */
    val memoryGb: Int
        get() = if (memTotalKb > 2L * 1024 * 1024) 3 else 2
}
