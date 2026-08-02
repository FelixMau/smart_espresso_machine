package de.fmau.shotcontrol.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import de.fmau.shotcontrol.data.StoredShot
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun HistoryScreen(
    shots: List<StoredShot>,
    onOpen: (StoredShot) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (shots.isEmpty()) {
        Box(modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text(
                "No shots yet.\nPull a shot and it lands here automatically.",
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = androidx.compose.ui.text.style.TextAlign.Center,
            )
        }
        return
    }

    LazyColumn(
        modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item { JournalSummary(shots) }
        items(shots, key = { it.id }) { shot ->
            ShotRow(shot, onClick = { onOpen(shot) })
        }
    }
}

@Composable
private fun JournalSummary(shots: List<StoredShot>) {
    val avgWeight = shots.map { it.finalWeightG }.average().toFloat()
    val avgDuration = shots.map { it.durationS }.average().toFloat()
    val onTarget = shots.count { kotlin.math.abs(it.finalWeightG - it.goalWeightG) <= 1f }

    SectionCard("Journal") {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            StatTile("Shots", "${shots.size}")
            StatTile("Avg weight", fmt(avgWeight), unit = "g")
            StatTile("Avg time", fmt(avgDuration), unit = "s")
            StatTile("Within 1 g", "$onTarget")
        }
    }
}

@Composable
private fun ShotRow(shot: StoredShot, onClick: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Row(
            Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(formatDate(shot.startedAt), style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        "${fmt(shot.finalWeightG)} g",
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        "  in ${fmt(shot.durationS)} s",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Text(
                    "goal ${fmt(shot.goalWeightG, 0)} g · ${fmt(shot.peakPressure)} bar peak · " +
                        shot.endReason.lowercase() +
                        if (shot.rating > 0) " · ${"★".repeat(shot.rating)}" else "",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            ShotSparkline(
                samples = shot.samples,
                modifier = Modifier.width(88.dp).height(40.dp),
            )
        }
    }
}

// Built per call: the locale can change while the app is running.
private fun formatDate(epochMs: Long): String =
    SimpleDateFormat("EEE d MMM · HH:mm", Locale.getDefault()).format(Date(epochMs))
