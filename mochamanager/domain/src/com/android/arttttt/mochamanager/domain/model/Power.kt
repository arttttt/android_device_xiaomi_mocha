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

/* One reading of the INA231 on the battery line. */
data class PowerSample(
    val voltageMv: Int,
    /* Positive while the battery is discharging. */
    val currentMa: Int,
    val powerMw: Int,
    /* Drawn since boot, as the power.stats HAL integrates it; null when it
     * does not answer. */
    val energyUWs: Long?,
)

enum class Supply { NONE, USB, AC }

data class Battery(
    val capacityPct: Int,
    val status: String,
    val health: String,
    val voltageUv: Int,
    val currentUa: Int,
    val tempDeciC: Int,
    val chargeNowUah: Int,
    val chargeFullUah: Int,
    val chargeFullDesignUah: Int,
    val cycleCount: Int,
    val supply: Supply,
    val chargingDisabled: Boolean,
) {
    /* What is left of the design capacity, in percent; null when the gauge
     * gives no design capacity. */
    val wearPercent: Double?
        get() = if (chargeFullDesignUah > 0) 100.0 * chargeFullUah / chargeFullDesignUah else null
}

/* The user's choice of how hard the CPU works; codes as the service has them. */
enum class PerformanceProfile(val code: Int) {
    POWER_SAVE(0),
    EFFICIENCY(3),
    BALANCED(1),
    PERFORMANCE(2);

    companion object {
        fun of(code: Int): PerformanceProfile? = values().firstOrNull { it.code == code }
    }
}
