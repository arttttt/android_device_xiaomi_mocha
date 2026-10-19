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
import com.android.arttttt.mochamanager.di.AppContainer
import com.android.arttttt.mochamanager.domain.model.AccelCalibration
import com.android.arttttt.mochamanager.domain.model.Accuracy
import com.android.arttttt.mochamanager.domain.model.Loadable
import com.android.arttttt.mochamanager.domain.model.SensorReading
import com.android.arttttt.mochamanager.domain.repository.CalibrationRepository
import com.android.arttttt.mochamanager.domain.repository.SensorRepository
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession
import com.android.arttttt.mochamanager.domain.usecase.AccelCalibrationSession.Step
import com.android.arttttt.mochamanager.domain.usecase.History
import com.android.arttttt.mochamanager.domain.usecase.adviseCalibration
import com.android.arttttt.mochamanager.ui.common.Effects
import com.android.arttttt.mochamanager.ui.common.STOP_TIMEOUT_MS
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.filterIsInstance
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onCompletion
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.onStart
import kotlinx.coroutines.flow.sample
import kotlinx.coroutines.flow.scan
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/*
 * The sensors the system lists and what they read, and the accelerometer's
 * calibration. Only the sensors on show are listened to: the folded-away
 * ones once they are unfolded. The readings come at hundreds a second; they
 * are gathered off the main thread and handed on a few times a second, which
 * is as often as anyone reads them.
 */
class SensorsViewModel(
    private val sensors: SensorRepository,
    private val calibrations: CalibrationRepository,
    background: CoroutineDispatcher,
) : ViewModel() {
    val effects = Effects<SensorsEffect>()

    private val groups = MutableStateFlow<Loadable<SensorGroups>>(Loadable.Loading)
    private val othersShown = MutableStateFlow(false)
    /* Bumped to listen again: the permission for the step sensors came. */
    private val listening = MutableStateFlow(0)
    private val current = MutableStateFlow<Loadable<AccelCalibration>>(Loadable.Loading)
    /* Kept here, not in the flow, so folding and leaving keep the seconds. */
    private val motion = MutableStateFlow(List(3) { History.empty(MOTION_SAMPLES) })
    /* Whether the user has asked for a calibration; it runs only while the
     * screen is shown -- it listens to the accelerometer at the game rate --
     * and is called off when the screen goes. */
    private val requested = MutableStateFlow(false)
    /* How the last one ended, until the user puts it away. */
    private val outcome = MutableStateFlow<AccelCalibrationSession.Progress?>(null)

    init {
        viewModelScope.launch { groups.value = Loadable.Ready(SensorGroups.of(sensors.sensors())) }
        loadCalibration()
    }

    private data class Gathered(
        val readings: Map<Int, SensorReading> = emptyMap(),
        val steps: Map<Int, Int> = emptyMap(),
    )

    private val gathered: Flow<Gathered> = combine(groups, othersShown, listening) { g, others, _ ->
        g.map { if (others) it.featured + it.others else it.featured }.valueOrNull to g.valueOrNull
    }
            .flatMapLatest { (listened, g) ->
                if (listened == null || g == null) return@flatMapLatest flowOf(Gathered())
                val detectors = listened.filter { it.type == Sensor.TYPE_STEP_DETECTOR }.map { it.key }.toSet()
                sensors.updates(listened)
                        .scan(Gathered()) { before, update ->
                            Gathered(before.readings + (update.key to update.reading),
                                    if (update.key in detectors) {
                                        before.steps + (update.key to (before.steps[update.key] ?: 0) + 1)
                                    } else before.steps)
                        }
                        .sample(RENDER_MS)
                        .onEach { shown -> g.accelerometer?.let { shown.readings[it.key] }?.let(::addMotion) }
            }
            .onStart { emit(Gathered()) }
            .flowOn(background)

    private fun addMotion(r: SensorReading) {
        motion.value = motion.value.mapIndexed { axis, h -> h.plus(r.values[axis]) }
    }

    /* A calibration moves at the accelerometer's rate; the screen needs no
     * more than ten steps a second of it. */
    private val progress: Flow<AccelCalibrationSession.Progress?> = combine(
            requested.flatMapLatest { wanted -> if (wanted) calibrate() else flowOf(null) }
                    .onCompletion { requested.value = false },
            outcome,
    ) { live, ended -> live ?: ended }.sample(PROGRESS_MS)

    private val shown = combine(gathered, motion, othersShown) { g, m, o -> Triple(g, m, o) }

    val state: StateFlow<SensorsState> = combine(groups, shown, current, progress) { g, (gathered, m, others), c, p ->
        val accelerometer = g.valueOrNull?.accelerometer
        val accuracy = accelerometer?.let { gathered.readings[it.key]?.accuracy } ?: Accuracy.UNKNOWN
        SensorsState(g, gathered.readings, gathered.steps, m, others, CalibrationState(
                available = sensors.uncalibratedResolution != null,
                current = c,
                accuracy = accuracy,
                advice = adviseCalibration(accuracy, c.valueOrNull),
                progress = p))
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), SensorsState())

    fun onAction(action: SensorsAction) {
        when (action) {
            SensorsAction.StartCalibration -> if (sensors.uncalibratedResolution != null) {
                outcome.value = null
                requested.value = true
            }
            SensorsAction.CloseCalibration -> {
                requested.value = false
                outcome.value = null
            }
            SensorsAction.ResetCalibration -> viewModelScope.launch {
                if (!calibrations.reset()) effects.send(SensorsEffect.ChangeFailed)
                loadCalibration()
            }
            SensorsAction.ToggleOthers -> othersShown.value = !othersShown.value
            SensorsAction.PermissionGranted -> listening.value++
        }
    }

    private fun loadCalibration() {
        viewModelScope.launch { current.value = Loadable.of(calibrations.calibration()) }
    }

    /*
     * One calibration, as a flow of where it stands: it starts listening
     * when collected and stops when it is no longer, whatever ends it. It
     * ends itself once it is made or has failed, leaving how it ended.
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
                            Step.DONE -> finish(p)
                            Step.FAILED -> end(p, SensorsEffect.CalibrationFailed)
                            else -> {}
                        }
                    }
                    before = p.step
                }
    }

    private fun finish(done: AccelCalibrationSession.Progress) {
        viewModelScope.launch {
            if (!calibrations.save(done.result!!)) effects.send(SensorsEffect.ChangeFailed)
            loadCalibration()
        }
        end(done, SensorsEffect.Calibrated)
    }

    private fun end(how: AccelCalibrationSession.Progress, effect: SensorsEffect) {
        effects.send(effect)
        outcome.value = how
        requested.value = false
    }

    companion object {
        private const val RENDER_MS = 200L
        private const val PROGRESS_MS = 100L
        /* Ten seconds at the render rate. */
        private const val MOTION_SAMPLES = 50

        fun create(c: AppContainer) = SensorsViewModel(c.sensors, c.calibration, Dispatchers.Default)
    }
}
