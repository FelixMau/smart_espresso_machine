package de.fmau.shotcontrol.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import de.fmau.shotcontrol.Connection
import java.util.Locale

/** A titled block of related controls. */
@Composable
fun SectionCard(
    title: String,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(Modifier.padding(16.dp)) {
            Text(
                title,
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(bottom = 12.dp),
            )
            content()
        }
    }
}

/**
 * A single number with its label. No chart — a bare value is the right form when
 * there is one number and no comparison to make.
 */
@Composable
fun StatTile(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    unit: String = "",
    emphasis: Boolean = false,
) {
    Column(modifier) {
        Text(
            label.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                value,
                style = if (emphasis) MaterialTheme.typography.displaySmall
                else MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.SemiBold,
            )
            if (unit.isNotEmpty()) {
                Text(
                    " $unit",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(bottom = 3.dp),
                )
            }
        }
    }
}

/** Connection state: dot plus word, never colour alone. */
@Composable
fun ConnectionChip(connection: Connection, scaleConnected: Boolean, modifier: Modifier = Modifier) {
    val (color, text) = when {
        connection == Connection.OFFLINE -> MaterialTheme.colorScheme.error to "Offline"
        connection == Connection.CONNECTING -> MaterialTheme.colorScheme.onSurfaceVariant to "Connecting"
        scaleConnected -> Color(0xFF1BAF7A) to "Ready"
        else -> Color(0xFFEDA100) to "No scale"
    }
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(Modifier.size(9.dp).background(color, CircleShape))
        Text(text, style = MaterialTheme.typography.labelLarge)
    }
}

/** Display formatting — deliberately locale-aware (a German user expects "36,0"). */
fun fmt(value: Float, decimals: Int = 1): String =
    String.format(Locale.getDefault(), "%.${decimals}f", value)
