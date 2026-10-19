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

package com.android.arttttt.mochamanager.ui.sensors

import android.hardware.Sensor
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import com.android.arttttt.mochamanager.domain.model.Accuracy
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.SensorInfo
import com.android.arttttt.mochamanager.domain.model.SensorReading
import com.android.arttttt.mochamanager.domain.repository.CalibrationRepository
import com.android.arttttt.mochamanager.domain.repository.SensorRepository
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession.Step
import com.android.arttttt.mochamanager.domain.usecase.adviseCalibration
import com.android.arttttt.mochamanager.ui.common.Effects
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.filterIsInstance
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onStart
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.sample
import kotlinx.coroutines.flow.scan
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/*
 * Every sensor the system lists and what it reads, and the accelerometer's
 * calibration. The readings come at hundreds a second; they are gathered off
 * the main thread and handed on a few times a second, which is as often as
 * anyone reads them.
 */
class SensorsViewModel(
    private val sensors: SensorRepository,
    private val calibrations: CalibrationRepository,
    background: CoroutineDispatcher,
) : ViewModel() {
    val effects = Effects<SensorsEffect>()

    private val list = MutableStateFlow<Loadable<List<SensorInfo>>>(Loadable.Loading)
    /* Bumped to listen again: the permission for the step sensors came. */
    private val listening = MutableStateFlow(0)
    private val current = MutableStateFlow<Loadable<AccelCalibration>>(Loadable.Loading)
    /* Whether the user has asked for a calibration; it runs only while the
     * screen is shown -- it listens to the accelerometer at the game rate --
     * and is called off when the screen goes. */
    private val requested = MutableStateFlow(false)

    init {
        viewModelScope.launch { list.value = Loadable.Ready(sensors.sensors()) }
        loadCalibration()
    }

    private data class Gathered(
        val readings: Map<Int, SensorReading> = emptyMap(),
        val steps: Map<Int, Int> = emptyMap(),
    )

    private val gathered: Flow<Gathered> = combine(list, listening) { l, _ -> l }
            .filterIsInstance<Loadable.Ready<List<SensorInfo>>>()
            .flatMapLatest { ready ->
                val detectors = ready.value.filter { it.type == Sensor.TYPE_STEP_DETECTOR }
                        .map { it.key }.toSet()
                sensors.updates(ready.value).scan(Gathered()) { g, update ->
                    Gathered(g.readings + (update.key to update.reading),
                            if (update.key in detectors) g.steps + (update.key to (g.steps[update.key] ?: 0) + 1)
                            else g.steps)
                }
            }
            .sample(RENDER_MS)
            .onStart { emit(Gathered()) }
            .flowOn(background)

    private val progress: Flow<AccelCalibrationSession.Progress?> = requested
            .flatMapLatest { wanted -> if (wanted) calibrate() else flowOf(null) }
            .onCompletion { requested.value = false }

    val state: StateFlow<SensorsState> = combine(list, gathered, current, progress) { l, g, c, p ->
        val primary = l.valueOrNull?.firstOrNull { it.primaryAccelerometer }
        val accuracy = primary?.let { g.readings[it.key]?.accuracy } ?: Accuracy.UNKNOWN
        SensorsState(l, g.readings, g.steps, CalibrationState(
                available = sensors.uncalibratedResolution != null,
                current = c,
                accuracy = accuracy,
                advice = adviseCalibration(accuracy, c.valueOrNull),
                progress = p))
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), SensorsState())

    fun onAction(action: SensorsAction) {
        when (action) {
            SensorsAction.StartCalibration -> startCalibration()
            SensorsAction.CancelCalibration -> stopCalibration()
            SensorsAction.ResetCalibration -> viewModelScope.launch {
                if (!calibrations.reset()) effects.send(SensorsEffect.ChangeFailed)
                loadCalibration()
            }
            SensorsAction.PermissionGranted -> listening.value++
        }
    }

    private fun loadCalibration() {
        viewModelScope.launch { current.value = Loadable.of(calibrations.calibration()) }
    }

    private fun startCalibration() {
        if (sensors.uncalibratedResolution != null) requested.value = true
    }

    private fun stopCalibration() {
        requested.value = false
    }

    /*
     * One calibration, as a flow of where it stands: it starts listening
     * when collected and stops when it is no longer, whatever ends it. It
     * ends itself once it is made or has failed.
     */
    private fun calibrate(): Flow<AccelCalibrationSession.Progress?> {
        val readings = sensors.uncalibratedAccelerometer() ?: return flowOf(null)
        val resolution = sensors.uncalibratedResolution ?: return flowOf(null)
        val session = AccelCalibrationSession(resolution)
        var before: Step? = null
        return readings
                .map { session.feed(it) }
                .onStart { emit(session.progress) }
                .onEach { p ->
                    if (p.step != before) {
                        when (p.step) {
                            Step.PLACE_DOWN -> if (before == Step.HOLD_UP) effects.send(SensorsEffect.PoseTaken)
                            Step.DONE -> finish(p.result!!)
                            Step.FAILED -> {
                                effects.send(SensorsEffect.CalibrationFailed)
                                stopCalibration()
                            }
                            else -> {}
                        }
                    }
                    before = p.step
                }
    }

    private fun finish(result: AccelCalibration) {
        effects.send(SensorsEffect.Calibrated)
        viewModelScope.launch {
            if (!calibrations.save(result)) effects.send(SensorsEffect.ChangeFailed)
            loadCalibration()
        }
        stopCalibration()
    }

    private companion object {
        const val RENDER_MS = 200L
    }
}
