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

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import androidx.lifecycle.Lifecycle
import com.google.android.material.bottomnavigation.BottomNavigationView

/*
 * One activity, a screen per tab. A screen is made the first time its tab is
 * shown and kept after that: switching tabs only hides one and shows the
 * other. A hidden screen is held at STARTED, so it pauses and stops reading
 * the service until it is shown again.
 */
class MainActivity : AppCompatActivity() {
    private val screens = linkedMapOf<Int, () -> Fragment>(
            R.id.tab_device to ::DeviceFragment,
            R.id.tab_power to ::PowerFragment,
            R.id.tab_monitor to ::MonitorFragment,
            R.id.tab_display to ::DisplayFragment)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val navigation = findViewById<BottomNavigationView>(R.id.navigation)
        navigation.setOnItemSelectedListener { item ->
            show(item.itemId)
            true
        }
        /* After a recreation the fragment manager has the screens back
         * already, under their tags; only which one is shown needs saying. */
        show(navigation.selectedItemId)
    }

    private fun show(tab: Int) {
        val fm = supportFragmentManager
        val transaction = fm.beginTransaction().setReorderingAllowed(true)
        for (id in screens.keys) {
            val existing = fm.findFragmentByTag(tag(id))
            if (id == tab) {
                if (existing == null) {
                    transaction.add(R.id.content, screens.getValue(id)(), tag(id))
                } else {
                    transaction.show(existing).setMaxLifecycle(existing, Lifecycle.State.RESUMED)
                }
            } else if (existing != null && !existing.isHidden) {
                transaction.hide(existing).setMaxLifecycle(existing, Lifecycle.State.STARTED)
            }
        }
        transaction.commit()
    }

    private fun tag(tab: Int) = "screen:" + resources.getResourceEntryName(tab)
}
