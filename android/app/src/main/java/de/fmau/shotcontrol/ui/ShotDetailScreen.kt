package de.fmau.shotcontrol.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.outlined.StarOutline
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import de.fmau.shotcontrol.data.StoredShot
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun ShotDetailScreen(
    shot: StoredShot,
    onUpdate: (StoredShot) -> Unit,
    onDelete: () -> Unit,
    onShare: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var note by remember(shot.id) { mutableStateOf(shot.note) }
    var confirmDelete by remember { mutableStateOf(false) }

    Column(
        modifier.verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(
            formatDate(shot.startedAt),
            style = MaterialTheme.typography.titleMedium,
        )

        ShotChart(
            samples = shot.samples,
            goalWeight = shot.goalWeightG,
            height = 300.dp,
        )

        SectionCard("Result") {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                StatTile("Weight", fmt(shot.finalWeightG), unit = "g")
                StatTile("Goal", fmt(shot.goalWeightG, 0), unit = "g")
                StatTile("Time", fmt(shot.durationS), unit = "s")
                StatTile("Peak", fmt(shot.peakPressure), unit = "bar")
            }
            Row(
                Modifier.fillMaxWidth().padding(top = 16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                StatTile("Avg flow", fmt(shot.avgFlow), unit = "g/s")
                StatTile("First drip", shot.firstDripS?.let { fmt(it) } ?: "–", unit = "s")
                StatTile("Ended by", shot.endReason.lowercase())
                StatTile("Samples", "${shot.samples.size}")
            }
        }

        SectionCard("Rating") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                (1..5).forEach { star ->
                    IconButton(onClick = {
                        // Tapping the current rating clears it.
                        onUpdate(shot.copy(rating = if (shot.rating == star) 0 else star))
                    }) {
                        Icon(
                            imageVector = if (star <= shot.rating) Icons.Default.Star
                            else Icons.Outlined.StarOutline,
                            contentDescription = "$star stars",
                        )
                    }
                }
            }
        }

        SectionCard("Notes") {
            OutlinedTextField(
                value = note,
                onValueChange = { note = it },
                modifier = Modifier.fillMaxWidth(),
                placeholder = { Text("Grinder setting, dose, bean, how it tasted…") },
                minLines = 3,
            )
            OutlinedButton(
                onClick = { onUpdate(shot.copy(note = note)) },
                enabled = note != shot.note,
                modifier = Modifier.padding(top = 8.dp),
            ) { Text("Save note") }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(onClick = onShare, modifier = Modifier.weight(1f)) {
                Icon(Icons.Default.Share, contentDescription = null)
                Text("  Export CSV")
            }
            OutlinedButton(
                onClick = { confirmDelete = true },
                modifier = Modifier.weight(1f),
            ) {
                Icon(Icons.Default.Delete, contentDescription = null)
                Text("  Delete")
            }
        }
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete this shot?") },
            text = { Text("The recorded curve and your note are removed from the journal.") },
            confirmButton = {
                TextButton(onClick = { confirmDelete = false; onDelete() }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { confirmDelete = false }) { Text("Cancel") }
            },
        )
    }
}

// Built per call: the locale can change while the app is running.
private fun formatDate(epochMs: Long): String =
    SimpleDateFormat("EEEE d MMMM yyyy, HH:mm", Locale.getDefault()).format(Date(epochMs))
