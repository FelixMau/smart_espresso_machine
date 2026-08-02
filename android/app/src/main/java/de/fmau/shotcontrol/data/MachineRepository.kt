package de.fmau.shotcontrol.data

import android.content.Context
import de.fmau.shotcontrol.Connection
import de.fmau.shotcontrol.IdleStyle
import de.fmau.shotcontrol.ScreenState
import de.fmau.shotcontrol.UiState
import de.fmau.shotcontrol.WakeReason
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.util.Locale

/**
 * Single owner of the connection to the machine.
 *
 * It lives outside the UI on purpose: in panel mode the screen goes dark, the
 * Activity stops, and something still has to notice that the scale woke up. The
 * foreground service holds this alive; the ViewModel is only a view onto it.
 */
object MachineRepository {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private lateinit var api: MachineApi
    lateinit var settings: AppSettings
        private set
    lateinit var store: ShotStore
        private set

    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    /** Emitted when the panel should light up again. */
    private val _wakeEvents = MutableSharedFlow<WakeReason>(extraBufferCapacity = 8)
    val wakeEvents: SharedFlow<WakeReason> = _wakeEvents.asSharedFlow()

    private var started = false

    /** Polling runs while the UI is visible, or while the panel service holds it up. */
    @Volatile private var uiVisible = false
    @Volatile private var serviceRunning = false

    private var liveShotId = -1
    private var fetchedSamples = 0
    private var shotStartedAtWall = 0L
    private var finalizeAtMs = 0L
    private var wasBrewing = false
    private var wasScaleConnected = false
    private var lastInteractionMs = System.currentTimeMillis()

    @Synchronized
    fun initialize(context: Context) {
        if (started) return
        started = true

        val app = context.applicationContext
        settings = AppSettings(app)
        store = ShotStore(app)
        api = MachineApi(settings.host)

        _state.update {
            it.copy(
                host = settings.host,
                autoSave = settings.autoSaveShots,
                liveIntervalMs = settings.liveIntervalMs,
                kioskEnabled = settings.kioskEnabled,
                idleStyle = runCatching { IdleStyle.valueOf(settings.idleStyle) }
                    .getOrDefault(IdleStyle.OVERLAY),
                idleTimeoutMinutes = settings.idleTimeoutMinutes,
            )
        }

        scope.launch { store.load() }
        scope.launch { pollLoop() }
        refreshProfile()
    }

    fun setUiVisible(visible: Boolean) {
        uiVisible = visible
        if (visible) noteInteraction()
    }

    fun setServiceRunning(running: Boolean) {
        serviceRunning = running
    }

    // ------------------------------------------------------------------ panel

    /** Any touch, or the UI coming forward: restarts the idle countdown. */
    fun noteInteraction() {
        lastInteractionMs = System.currentTimeMillis()
    }

    fun wake(reason: WakeReason) {
        noteInteraction()
        if (_state.value.screen == ScreenState.IDLE) {
            _state.update { it.copy(screen = ScreenState.ACTIVE) }
            _wakeEvents.tryEmit(reason)
        }
    }

    /** Go dark now, without waiting for the timeout. */
    fun goDark() {
        if (_state.value.kioskEnabled) {
            _state.update { it.copy(screen = ScreenState.IDLE) }
        }
    }

    private fun updateIdleState(status: MachineStatus) {
        val s = _state.value
        if (!s.kioskEnabled) {
            if (s.screen != ScreenState.ACTIVE) {
                _state.update { it.copy(screen = ScreenState.ACTIVE) }
            }
            return
        }

        // Never go dark mid-shot, whatever the timer says.
        if (status.brewing) {
            noteInteraction()
            return
        }

        val idleAfterMs = s.idleTimeoutMinutes.coerceAtLeast(1) * 60_000L
        if (s.screen == ScreenState.ACTIVE &&
            System.currentTimeMillis() - lastInteractionMs > idleAfterMs
        ) {
            _state.update { it.copy(screen = ScreenState.IDLE) }
        }
    }

    // ---------------------------------------------------------------- polling

    private suspend fun pollLoop() {
        while (true) {
            if (!uiVisible && !serviceRunning) {
                delay(500)
                continue
            }
            try {
                val status = api.status()
                _state.update {
                    it.copy(status = status, connection = Connection.ONLINE, error = null)
                }
                trackShot(status)
                updateIdleState(status)
            } catch (e: Exception) {
                _state.update {
                    it.copy(
                        connection = Connection.OFFLINE,
                        error = e.message ?: e.javaClass.simpleName,
                    )
                }
            }
            delay(currentInterval())
        }
    }

    private fun currentInterval(): Long {
        val s = _state.value
        return when {
            s.status.brewing -> settings.liveIntervalMs
            s.screen == ScreenState.IDLE -> settings.darkIntervalMs
            else -> settings.idleIntervalMs
        }
    }

    private suspend fun trackShot(status: MachineStatus) {
        // The scale coming to life is the earliest sign that coffee is happening.
        if (status.scaleConnected && !wasScaleConnected) {
            wake(WakeReason.SCALE_CONNECTED)
        }
        wasScaleConnected = status.scaleConnected

        if (status.shotId != liveShotId) {
            liveShotId = status.shotId
            fetchedSamples = 0
            finalizeAtMs = 0L
            shotStartedAtWall = System.currentTimeMillis() - (status.shotTime * 1000).toLong()
            _state.update { it.copy(liveSamples = emptyList()) }
        }

        if (status.datapoints > fetchedSamples) {
            runCatching { api.shot(fetchedSamples) }.getOrNull()?.let { curve ->
                if (curve.shotId == liveShotId && curve.points.isNotEmpty()) {
                    fetchedSamples = curve.total
                    _state.update { it.copy(liveSamples = it.liveSamples + curve.points) }
                }
            }
        }

        if (wasBrewing && !status.brewing) {
            // Wait out the firmware's drip delay so the final weight has settled.
            finalizeAtMs = System.currentTimeMillis() + FINALIZE_DELAY_MS
        }
        wasBrewing = status.brewing

        if (finalizeAtMs != 0L && System.currentTimeMillis() >= finalizeAtMs && !status.brewing) {
            finalizeAtMs = 0L
            if (settings.autoSaveShots) saveCurrentShot(silent = true)
        }
    }

    // ---------------------------------------------------------------- journal

    fun saveCurrentShot(silent: Boolean = false) {
        scope.launch {
            try {
                val curve = api.shot(0)
                val samples = curve.points
                val duration =
                    if (curve.duration > 0f) curve.duration else samples.lastOrNull()?.t ?: 0f

                if (duration < settings.minSaveDurationS) {
                    if (!silent) message("Too short to save (${fmt(duration)} s)")
                    return@launch
                }
                if (samples.isEmpty()) {
                    if (!silent) message("No samples recorded")
                    return@launch
                }

                val startedAt = if (shotStartedAtWall > 0) shotStartedAtWall
                else System.currentTimeMillis() - ((duration + 3f) * 1000).toLong()

                val id = "${startedAt}-${curve.shotId}"
                if (store.byId(id) != null) return@launch

                val finalWeight =
                    if (curve.finalWeight > 0f) curve.finalWeight else samples.maxOf { it.w }

                store.save(
                    StoredShot(
                        id = id,
                        shotId = curve.shotId,
                        startedAt = startedAt,
                        durationS = duration,
                        finalWeightG = finalWeight,
                        goalWeightG = curve.goalWeight,
                        endReason = curve.end,
                        samples = samples,
                    )
                )
                message("Shot saved (${fmt(finalWeight)} g)")
            } catch (e: Exception) {
                message("Could not save shot: ${e.message}")
            }
        }
    }

    fun updateShot(shot: StoredShot) = scope.launch { store.save(shot) }

    fun deleteShot(id: String) = scope.launch { store.delete(id) }

    // ---------------------------------------------------------------- control

    fun start() = command("Start requested") { api.start() }

    fun stop() = command("Stop requested") { api.stop() }

    fun setGoalWeight(grams: Float) = command("Goal weight ${fmt(grams)} g") {
        api.setGoalWeight(grams)
    }

    fun setWeightOffset(grams: Float) = command("Offset ${fmt(grams)} g") {
        api.setWeightOffset(grams)
    }

    fun applyProfile(profile: PressureProfile) = command("Pressure profile sent") {
        api.setPressureProfile(profile)
        _state.update { it.copy(profile = profile) }
    }

    fun refreshProfile() {
        scope.launch {
            runCatching { api.profile() }.getOrNull()?.let { p ->
                _state.update { it.copy(profile = p) }
            }
        }
    }

    private fun command(okMessage: String, block: suspend () -> Unit) {
        scope.launch {
            try {
                block()
                message(okMessage)
                runCatching { api.status() }.getOrNull()?.let { s ->
                    _state.update { it.copy(status = s) }
                }
            } catch (e: Exception) {
                message(e.message ?: "Request failed")
            }
        }
    }

    // --------------------------------------------------------------- settings

    fun setHost(host: String) {
        settings.host = host
        api.host = host
        liveShotId = -1
        _state.update { it.copy(host = host, connection = Connection.CONNECTING, error = null) }
        refreshProfile()
    }

    fun discover(context: Context) {
        scope.launch {
            _state.update { it.copy(discovering = true, discovered = emptyList()) }
            val results = Discovery.find(context.applicationContext)
            _state.update { it.copy(discovering = false, discovered = results) }
            if (results.isEmpty()) message("No machine found on this network")
        }
    }

    fun setAutoSave(value: Boolean) {
        settings.autoSaveShots = value
        _state.update { it.copy(autoSave = value) }
    }

    fun setLiveInterval(ms: Long) {
        settings.liveIntervalMs = ms
        _state.update { it.copy(liveIntervalMs = ms) }
    }

    fun setKioskEnabled(value: Boolean) {
        settings.kioskEnabled = value
        noteInteraction()
        _state.update {
            it.copy(kioskEnabled = value, screen = ScreenState.ACTIVE)
        }
    }

    fun setIdleStyle(style: IdleStyle) {
        settings.idleStyle = style.name
        _state.update { it.copy(idleStyle = style) }
    }

    fun setIdleTimeoutMinutes(minutes: Int) {
        settings.idleTimeoutMinutes = minutes
        noteInteraction()
        _state.update { it.copy(idleTimeoutMinutes = minutes) }
    }

    fun setDeviceAdminActive(active: Boolean) {
        _state.update { it.copy(deviceAdminActive = active) }
    }

    fun message(text: String) = _state.update { it.copy(message = text) }

    fun consumeMessage() = _state.update { it.copy(message = null) }

    private fun fmt(v: Float) = String.format(Locale.getDefault(), "%.1f", v)

    private const val FINALIZE_DELAY_MS = 4500L
}
