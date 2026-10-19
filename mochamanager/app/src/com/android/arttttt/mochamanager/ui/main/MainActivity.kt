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

package com.android.arttttt.mochamanager.ui.main

import android.os.Bundle
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.android.arttttt.mochamanager.R
import com.google.android.material.bottomnavigation.BottomNavigationView
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch

/*
 * One activity, a screen per tab. A screen is made the first time its tab is
 * shown and kept after that: switching tabs only hides one and shows the
 * other. A hidden screen is held at STARTED, so it stops collecting its
 * model's state until it is shown again.
 */
class MainActivity : AppCompatActivity() {
    private val model: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val navigation = findViewById<BottomNavigationView>(R.id.navigation)
        navigation.setOnItemSelectedListener { item ->
            model.select(Tab.of(item.itemId))
            true
        }

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                model.state.collect { tab ->
                    if (navigation.selectedItemId != tab.itemId) navigation.selectedItemId = tab.itemId
                    show(tab)
                }
            }
        }
    }

    private fun show(tab: Tab) {
        val fm = supportFragmentManager
        val transaction = fm.beginTransaction().setReorderingAllowed(true)
        for (other in Tab.values()) {
            val existing = fm.findFragmentByTag(other.tag)
            if (other == tab) {
                if (existing == null) {
                    transaction.add(R.id.content, other.create(), other.tag)
                } else {
                    transaction.show(existing).setMaxLifecycle(existing, Lifecycle.State.RESUMED)
                }
            } else if (existing != null && !existing.isHidden) {
                transaction.hide(existing).setMaxLifecycle(existing, Lifecycle.State.STARTED)
            }
        }
        transaction.commit()
    }
}
