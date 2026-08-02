package de.fmau.shotcontrol

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import de.fmau.shotcontrol.data.MachineRepository
import de.fmau.shotcontrol.data.PressureProfile
import de.fmau.shotcontrol.data.ShotStore
import de.fmau.shotcontrol.data.StoredShot
import kotlinx.coroutines.flow.StateFlow

/**
 * A view onto [MachineRepository]. It holds no state of its own: in panel mode the
 * Activity is torn down while the repository (held up by the panel service) keeps
 * polling, so anything stateful here would be lost exactly when it matters.
 */
class MachineViewModel(app: Application) : AndroidViewModel(app) {

    private val repo = MachineRepository.also { it.initialize(app) }

    val state: StateFlow<UiState> get() = repo.state
    val shots: StateFlow<List<StoredShot>> get() = repo.store.shots
    val store: ShotStore get() = repo.store

    fun setUiVisible(visible: Boolean) = repo.setUiVisible(visible)
    fun noteInteraction() = repo.noteInteraction()
    fun wake(reason: WakeReason) = repo.wake(reason)
    fun goDark() = repo.goDark()

    fun start() = repo.start()
    fun stop() = repo.stop()
    fun setGoalWeight(grams: Float) = repo.setGoalWeight(grams)
    fun setWeightOffset(grams: Float) = repo.setWeightOffset(grams)
    fun applyProfile(profile: PressureProfile) = repo.applyProfile(profile)
    fun saveCurrentShot() = repo.saveCurrentShot()

    fun updateShot(shot: StoredShot) = repo.updateShot(shot)
    fun deleteShot(id: String) = repo.deleteShot(id)

    fun setHost(host: String) = repo.setHost(host)
    fun discover() = repo.discover(getApplication<Application>())
    fun setAutoSave(value: Boolean) = repo.setAutoSave(value)
    fun setLiveInterval(ms: Long) = repo.setLiveInterval(ms)

    fun setKioskEnabled(value: Boolean) = repo.setKioskEnabled(value)
    fun setIdleStyle(style: IdleStyle) = repo.setIdleStyle(style)
    fun setIdleTimeoutMinutes(minutes: Int) = repo.setIdleTimeoutMinutes(minutes)
    fun setDeviceAdminActive(active: Boolean) = repo.setDeviceAdminActive(active)

    fun consumeMessage() = repo.consumeMessage()
}
