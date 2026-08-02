package de.fmau.shotcontrol

import de.fmau.shotcontrol.data.DiscoveredMachine
import de.fmau.shotcontrol.data.MachineStatus
import de.fmau.shotcontrol.data.PressureProfile
import de.fmau.shotcontrol.data.Sample

enum class Connection { CONNECTING, ONLINE, OFFLINE }

/** Panel mode: the screen is either showing something or deliberately dark. */
enum class ScreenState { ACTIVE, IDLE }

/** How the screen is darkened while idle. */
enum class IdleStyle {
    /** Black surface at zero brightness. No permissions; a tap wakes it. */
    OVERLAY,

    /** Real screen-off through Device Admin `lockNow()`. */
    SCREEN_OFF,
}

/** Why the panel came back to life — kept for the log line, and to explain itself. */
enum class WakeReason { SCALE_CONNECTED, TOUCH, MANUAL }

data class UiState(
    val connection: Connection = Connection.CONNECTING,
    val status: MachineStatus = MachineStatus(),
    val liveSamples: List<Sample> = emptyList(),
    val profile: PressureProfile = PressureProfile(),
    val host: String = "",
    val error: String? = null,
    val message: String? = null,
    val discovering: Boolean = false,
    val discovered: List<DiscoveredMachine> = emptyList(),
    val autoSave: Boolean = true,
    val liveIntervalMs: Long = 300L,

    // --- panel / kiosk ---
    val kioskEnabled: Boolean = false,
    val idleStyle: IdleStyle = IdleStyle.OVERLAY,
    val idleTimeoutMinutes: Int = 5,
    val screen: ScreenState = ScreenState.ACTIVE,
    val deviceAdminActive: Boolean = false,
)
