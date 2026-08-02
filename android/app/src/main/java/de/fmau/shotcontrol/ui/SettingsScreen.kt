package de.fmau.shotcontrol.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.text.KeyboardOptions
import de.fmau.shotcontrol.IdleStyle
import de.fmau.shotcontrol.UiState
import de.fmau.shotcontrol.data.PressureProfile

@Composable
fun SettingsScreen(
    state: UiState,
    onHost: (String) -> Unit,
    onDiscover: () -> Unit,
    onAutoSave: (Boolean) -> Unit,
    onLiveInterval: (Long) -> Unit,
    onProfile: (PressureProfile) -> Unit,
    onExportJournal: () -> Unit,
    onKiosk: (Boolean) -> Unit,
    onIdleStyle: (IdleStyle) -> Unit,
    onIdleTimeout: (Int) -> Unit,
    onRequestDeviceAdmin: () -> Unit,
    onGoDarkNow: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        MachineCard(state, onHost, onDiscover)
        PanelCard(state, onKiosk, onIdleStyle, onIdleTimeout, onRequestDeviceAdmin, onGoDarkNow)
        ProfileCard(state.profile, onProfile)
        RecordingCard(
            autoSave = state.autoSave,
            liveIntervalMs = state.liveIntervalMs,
            onAutoSave = onAutoSave,
            onLiveInterval = onLiveInterval,
            onExportJournal = onExportJournal,
        )

        Text(
            "Shot Control talks to the ESP32 over plain HTTP on your local " +
                "network. Endpoints live in src/webserver.h.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun MachineCard(state: UiState, onHost: (String) -> Unit, onDiscover: () -> Unit) {
    var host by remember(state.host) { mutableStateOf(state.host) }

    SectionCard("Machine address") {
        OutlinedTextField(
            value = host,
            onValueChange = { host = it },
            label = { Text("Host or IP") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
        )
        Row(
            Modifier.padding(top = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Button(onClick = { onHost(host) }, enabled = host.isNotBlank() && host != state.host) {
                Text("Connect")
            }
            OutlinedButton(onClick = onDiscover, enabled = !state.discovering) {
                if (state.discovering) {
                    CircularProgressIndicator(Modifier.width(16.dp), strokeWidth = 2.dp)
                } else {
                    Icon(Icons.Default.Search, contentDescription = null)
                }
                Text("  Find on network")
            }
        }

        state.discovered.forEach { machine ->
            Text(
                "${machine.name} — ${machine.address}",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp)
                    .clickable { host = machine.address; onHost(machine.address) },
            )
        }

        state.error?.let {
            Text(
                "Last error: $it",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error,
                modifier = Modifier.padding(top = 8.dp),
            )
        }
    }
}

/** Panel mode: the phone lives on the machine and should look switched off. */
@Composable
private fun PanelCard(
    state: UiState,
    onKiosk: (Boolean) -> Unit,
    onIdleStyle: (IdleStyle) -> Unit,
    onIdleTimeout: (Int) -> Unit,
    onRequestDeviceAdmin: () -> Unit,
    onGoDarkNow: () -> Unit,
) {
    SectionCard("Panel mode") {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text("Mounted on the machine", style = MaterialTheme.typography.bodyLarge)
                Text(
                    "Goes dark after ${state.idleTimeoutMinutes} min of nothing, " +
                        "wakes when the scale connects or you touch it. " +
                        "Keeps polling in the background.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Switch(checked = state.kioskEnabled, onCheckedChange = onKiosk)
        }

        if (state.kioskEnabled) {
            Text(
                "Idle after",
                style = MaterialTheme.typography.labelLarge,
                modifier = Modifier.padding(top = 16.dp),
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(1, 2, 5, 10, 30).forEach { minutes ->
                    FilterChip(
                        selected = state.idleTimeoutMinutes == minutes,
                        onClick = { onIdleTimeout(minutes) },
                        label = { Text("$minutes min") },
                    )
                }
            }

            Text(
                "When dark",
                style = MaterialTheme.typography.labelLarge,
                modifier = Modifier.padding(top = 16.dp),
            )
            Column {
                IdleStyleOption(
                    selected = state.idleStyle == IdleStyle.OVERLAY,
                    title = "Black screen",
                    detail = "Backlight to zero, tap anywhere to wake. No permissions.",
                    onClick = { onIdleStyle(IdleStyle.OVERLAY) },
                )
                IdleStyleOption(
                    selected = state.idleStyle == IdleStyle.SCREEN_OFF,
                    title = "Display off",
                    detail = if (state.deviceAdminActive) {
                        "Really switches the display off. Wakes on the scale, or the power button."
                    } else {
                        "Needs device admin — grant it below, or this falls back to a black screen."
                    },
                    onClick = { onIdleStyle(IdleStyle.SCREEN_OFF) },
                )
            }

            if (state.idleStyle == IdleStyle.SCREEN_OFF && !state.deviceAdminActive) {
                OutlinedButton(
                    onClick = onRequestDeviceAdmin,
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                ) { Text("Grant device admin (screen lock only)") }
            }

            OutlinedButton(
                onClick = onGoDarkNow,
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            ) { Text("Go dark now") }
        }
    }
}

@Composable
private fun IdleStyleOption(
    selected: Boolean,
    title: String,
    detail: String,
    onClick: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(selected = selected, onClick = onClick)
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            Text(
                detail,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun ProfileCard(profile: PressureProfile, onApply: (PressureProfile) -> Unit) {
    // Editable copies; the machine keeps two lists (by elapsed time, by time left).
    val byTime = remember(profile) {
        mutableStateListOf(*profile.byTime.map { it.first.toString() to it.second.toString() }
            .toTypedArray())
    }
    val byTimeLeft = remember(profile) {
        mutableStateListOf(*profile.byTimeLeft.map { it.first.toString() to it.second.toString() }
            .toTypedArray())
    }

    SectionCard("Pressure profile") {
        Text(
            "Target pressure at a point in the shot. The second list overrides the " +
                "first once the predicted end is that many seconds away.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(bottom = 12.dp),
        )

        GoalList("At second", byTime)
        GoalList("With seconds left", byTimeLeft)

        Button(
            onClick = {
                onApply(
                    PressureProfile(
                        byTime = byTime.mapNotNull { it.toFloats() },
                        byTimeLeft = byTimeLeft.mapNotNull { it.toFloats() },
                    )
                )
            },
            modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
        ) { Text("Send profile to machine") }
    }
}

private fun Pair<String, String>.toFloats(): Pair<Float, Float>? {
    val t = first.toFloatOrNull() ?: return null
    val p = second.toFloatOrNull() ?: return null
    return t to p
}

@Composable
private fun GoalList(label: String, goals: androidx.compose.runtime.snapshots.SnapshotStateList<Pair<String, String>>) {
    Text(label, style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(top = 8.dp))

    goals.forEachIndexed { index, (time, pressure) ->
        Row(
            Modifier.fillMaxWidth().padding(vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedTextField(
                value = time,
                onValueChange = { goals[index] = it to goals[index].second },
                label = { Text("s") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.weight(1f),
            )
            OutlinedTextField(
                value = pressure,
                onValueChange = { goals[index] = goals[index].first to it },
                label = { Text("bar") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.weight(1f),
            )
            IconButton(onClick = { goals.removeAt(index) }) {
                Icon(Icons.Default.Close, contentDescription = "Remove goal")
            }
        }
    }

    OutlinedButton(
        onClick = { if (goals.size < 8) goals.add("" to "") },
        enabled = goals.size < 8,
    ) {
        Icon(Icons.Default.Add, contentDescription = null)
        Text("  Add point")
    }
}

@Composable
private fun RecordingCard(
    autoSave: Boolean,
    liveIntervalMs: Long,
    onAutoSave: (Boolean) -> Unit,
    onLiveInterval: (Long) -> Unit,
    onExportJournal: () -> Unit,
) {
    var interval by remember(liveIntervalMs) { mutableStateOf(liveIntervalMs.toString()) }

    SectionCard("Recording") {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text("Save every shot", style = MaterialTheme.typography.bodyLarge)
                Text(
                    "Files a finished shot in the journal on its own",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Switch(checked = autoSave, onCheckedChange = onAutoSave)
        }

        Row(
            Modifier.fillMaxWidth().padding(top = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedTextField(
                value = interval,
                onValueChange = { interval = it },
                label = { Text("Live poll interval (ms)") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.weight(1f),
            )
            OutlinedButton(
                onClick = { interval.toLongOrNull()?.coerceIn(100L, 5000L)?.let(onLiveInterval) },
            ) { Text("Apply") }
        }

        OutlinedButton(
            onClick = onExportJournal,
            modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
        ) { Text("Export whole journal as JSON") }
    }
}
