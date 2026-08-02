package de.fmau.shotcontrol.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import de.fmau.shotcontrol.Connection
import de.fmau.shotcontrol.UiState

/**
 * Everything the built-in web page offers, in thumb reach: live readings,
 * start/stop, goal weight, weight offset — plus the curve as it is pulled.
 */
@Composable
fun LiveScreen(
    state: UiState,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onGoalWeight: (Float) -> Unit,
    onWeightOffset: (Float) -> Unit,
    onSaveShot: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val status = state.status
    val online = state.connection == Connection.ONLINE

    Column(
        modifier
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        // ---- headline readings ------------------------------------------
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.Bottom,
        ) {
            StatTile("Weight", fmt(status.weight), unit = "g", emphasis = true)
            StatTile("Pressure", fmt(status.pressure), unit = "bar")
            StatTile("Time", fmt(status.shotTime), unit = "s")
        }

        if (status.brewing) {
            val progress = if (status.goalWeight > 0f) {
                (status.weight / status.goalWeight).coerceIn(0f, 1f)
            } else 0f
            Column {
                LinearProgressIndicator(
                    progress = { progress },
                    modifier = Modifier.fillMaxWidth().height(6.dp),
                )
                Text(
                    "Target ${fmt(status.goalWeight, 0)} g · predicted end " +
                        (if (status.expectedEnd in 0f..120f) "${fmt(status.expectedEnd)} s" else "–"),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 6.dp),
                )
            }
        } else if (status.lastDuration > 0f) {
            Text(
                "Last shot: ${fmt(status.lastDuration)} s · " +
                    "${fmt(status.finalWeight)} g · ended by ${status.lastEnd.lowercase()}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        // ---- brew control ------------------------------------------------
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(
                onClick = onStart,
                enabled = online && !status.brewing,
                modifier = Modifier.weight(1f).height(56.dp),
            ) {
                Icon(Icons.Default.PlayArrow, contentDescription = null)
                Text("  Start")
            }
            Button(
                onClick = onStop,
                enabled = online && status.brewing,
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.error,
                    contentColor = MaterialTheme.colorScheme.onError,
                ),
                modifier = Modifier.weight(1f).height(56.dp),
            ) {
                Icon(Icons.Default.Stop, contentDescription = null)
                Text("  Stop")
            }
        }

        // ---- curve --------------------------------------------------------
        SectionCard("Extraction") {
            ShotChart(
                samples = state.liveSamples,
                goalWeight = status.goalWeight,
                profile = state.profile,
                expectedEnd = status.expectedEnd.takeIf { it in 0f..120f },
            )
            OutlinedButton(
                onClick = onSaveShot,
                enabled = online && !status.brewing,
                modifier = Modifier.padding(top = 12.dp),
            ) {
                Icon(Icons.Default.Save, contentDescription = null)
                Text("  Save last shot")
            }
        }

        // ---- goal weight ---------------------------------------------------
        GoalWeightCard(status.goalWeight, online, onGoalWeight)

        // ---- weight offset -------------------------------------------------
        WeightOffsetCard(status.weightOffset, online, onWeightOffset)

        // ---- machine state -------------------------------------------------
        SectionCard("Machine") {
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                StatTile("Scale", if (status.scaleConnected) "linked" else "searching")
                StatTile("Pump", "${(status.pumpPwm * 100 / 255)}", unit = "%")
                StatTile("Goal p.", fmt(status.goalPressure), unit = "bar")
                StatTile("WiFi", "${status.rssi}", unit = "dBm")
            }
        }
    }
}

@Composable
private fun GoalWeightCard(goalWeight: Float, online: Boolean, onChange: (Float) -> Unit) {
    var pending by remember(goalWeight) { mutableFloatStateOf(goalWeight) }

    SectionCard("Goal weight") {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedButton(
                onClick = { pending = (pending - 1f).coerceAtLeast(10f) },
                enabled = online,
            ) { Text("−1") }

            Text(
                "${fmt(pending)} g",
                style = MaterialTheme.typography.headlineSmall,
                modifier = Modifier.weight(1f),
                textAlign = androidx.compose.ui.text.style.TextAlign.Center,
            )

            OutlinedButton(
                onClick = { pending = (pending + 1f).coerceAtMost(120f) },
                enabled = online,
            ) { Text("+1") }
        }
        Slider(
            value = pending,
            onValueChange = { pending = Math.round(it * 2f) / 2f },
            valueRange = 10f..80f,
            enabled = online,
        )
        Button(
            onClick = { onChange(pending) },
            enabled = online && pending != goalWeight,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Set goal weight") }
    }
}

@Composable
private fun WeightOffsetCard(offset: Float, online: Boolean, onChange: (Float) -> Unit) {
    var pending by remember(offset) { mutableFloatStateOf(offset) }

    // Reflect learned offsets that arrive while the slider is untouched.
    LaunchedEffect(offset) { pending = offset }

    SectionCard("Weight offset") {
        Text(
            "Stops the shot this much early to compensate for drips. " +
                "The machine learns this value after every shot.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            Slider(
                value = pending,
                onValueChange = { pending = Math.round(it * 10f) / 10f },
                valueRange = 0f..5f,
                enabled = online,
                modifier = Modifier.weight(1f),
            )
            Text("  ${fmt(pending)} g", style = MaterialTheme.typography.titleMedium)
        }
        Button(
            onClick = { onChange(pending) },
            enabled = online && pending != offset,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Set offset") }
    }
}
