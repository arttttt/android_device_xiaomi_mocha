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

package com.android.arttttt.mochamanager.domain

import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.usecase.poll
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test

class PollingTest {
    @Test
    fun readsEveryPeriodAndSaysWhenThereIsNothing() = runBlocking {
        var n = 0
        val got = poll(1) { (++n).takeIf { it != 2 } }.take(3).toList()
        assertEquals(listOf(Loadable.Ready(1), Loadable.Unavailable, Loadable.Ready(3)), got)
    }

    @Test
    fun aPeriodOfNothingReadsOnce() = runBlocking {
        val got = poll(0) { "once" }.toList()
        assertEquals(listOf(Loadable.Ready("once")), got)
    }
}
