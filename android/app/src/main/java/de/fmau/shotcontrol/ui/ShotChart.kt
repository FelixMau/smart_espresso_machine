package de.fmau.shotcontrol.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import de.fmau.shotcontrol.data.PressureProfile
import de.fmau.shotcontrol.data.Sample
import de.fmau.shotcontrol.ui.theme.VizColors
import de.fmau.shotcontrol.ui.theme.viz
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.max

/**
 * The extraction curve: weight and pressure over the same time axis, in two
 * stacked panels.
 *
 * Two panels rather than one plot with two y-scales — a dual axis lets the
 * reader infer crossings that do not exist. Sharing the x axis keeps the
 * comparison ("pressure dropped, flow picked up") intact.
 */
@Composable
fun ShotChart(
    samples: List<Sample>,
    modifier: Modifier = Modifier,
    goalWeight: Float? = null,
    profile: PressureProfile? = null,
    expectedEnd: Float? = null,
    height: Dp = 280.dp,
) {
    val colors = viz
    val measurer = rememberTextMeasurer()
    var scrubT by remember { mutableStateOf<Float?>(null) }

    val scrubbed = remember(samples, scrubT) {
        scrubT?.let { t -> samples.minByOrNull { abs(it.t - t) } }
    }
    val readout = scrubbed ?: samples.lastOrNull()

    Column(modifier) {
        Row(
            Modifier.fillMaxWidth().padding(bottom = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SeriesKey("Weight", format(readout?.w) + " g", colors.weight)
            SeriesKey("Pressure", format(readout?.p) + " bar", colors.pressure)
            Text(
                text = format(readout?.t) + " s",
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        Box(
            Modifier
                .fillMaxWidth()
                .height(height)
                .background(colors.surface, RoundedCornerShape(12.dp))
        ) {
            if (samples.isEmpty()) {
                Text(
                    "No samples yet",
                    modifier = Modifier.align(Alignment.Center),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                Canvas(
                    Modifier
                        .fillMaxWidth()
                        .height(height)
                        // Touch anywhere to read the curve at that moment; slide to
                        // scan. Events are not consumed, so the page still scrolls.
                        .pointerInput(samples) {
                            awaitEachGesture {
                                val down = awaitFirstDown(requireUnconsumed = false)
                                scrubT = tForX(down.position.x, size.width.toFloat(), samples)
                                do {
                                    val event = awaitPointerEvent()
                                    event.changes.firstOrNull()?.let { change ->
                                        scrubT = tForX(change.position.x, size.width.toFloat(), samples)
                                    }
                                } while (event.changes.any { it.pressed })
                                scrubT = null
                            }
                        }
                ) {
                    drawShotChart(
                        samples = samples,
                        colors = colors,
                        measurer = measurer,
                        goalWeight = goalWeight,
                        profile = profile,
                        expectedEnd = expectedEnd,
                        scrub = scrubbed,
                    )
                }
            }
        }
    }
}

@Composable
private fun SeriesKey(label: String, value: String, color: Color) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(10.dp).background(color, CircleShape))
        Text(
            "  $label ",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(value, style = MaterialTheme.typography.labelLarge)
    }
}

/** Maps a touch x position back to a time on the shared time axis. */
private fun Density.tForX(x: Float, width: Float, samples: List<Sample>): Float {
    val tMax = max(samples.last().t, 5f)
    val left = 40.dp.toPx()
    val plotW = (width - left - 12.dp.toPx()).coerceAtLeast(1f)
    return ((x - left) / plotW * tMax).coerceIn(0f, tMax)
}

private fun format(v: Float?): String = if (v == null) "–" else fmt(v)

private fun niceMax(v: Float, step: Float): Float =
    max(step, ceil(v / step) * step)

private fun DrawScope.drawShotChart(
    samples: List<Sample>,
    colors: VizColors,
    measurer: TextMeasurer,
    goalWeight: Float?,
    profile: PressureProfile?,
    expectedEnd: Float?,
    scrub: Sample?,
) {
    val labelStyle = TextStyle(fontSize = 10.sp, color = colors.axisText)

    val left = 40.dp.toPx()
    val right = 12.dp.toPx()
    val top = 10.dp.toPx()
    val axisH = 18.dp.toPx()
    val gap = 18.dp.toPx()

    val plotW = (size.width - left - right).coerceAtLeast(1f)
    val plotsH = (size.height - top - axisH - gap).coerceAtLeast(1f)
    val wTop = top
    val wBottom = wTop + plotsH * 0.55f
    val pTop = wBottom + gap
    val pBottom = pTop + plotsH * 0.45f

    val tMax = max(samples.last().t, 5f)
    val wMax = niceMax(max(samples.maxOf { it.w }, goalWeight ?: 0f) * 1.1f, 10f)
    val pMax = niceMax(max(samples.maxOf { it.p }, 6f) * 1.1f, 3f)

    fun x(t: Float) = left + (t / tMax) * plotW
    fun yw(w: Float) = wBottom - (w / wMax) * (wBottom - wTop)
    fun yp(p: Float) = pBottom - (p / pMax) * (pBottom - pTop)

    // ---- grid + y labels (recessive) -------------------------------------
    fun panelGrid(top: Float, bottom: Float, maxValue: Float, steps: Int, unitLabel: String) {
        for (i in 0..steps) {
            val value = maxValue * i / steps
            val y = bottom - (value / maxValue) * (bottom - top)
            drawLine(colors.grid, Offset(left, y), Offset(left + plotW, y), strokeWidth = 1f)
            val text = if (maxValue >= 10f) value.toInt().toString() else fmt(value)
            val layout = measurer.measure(text, labelStyle)
            drawText(
                layout,
                topLeft = Offset(left - 6f - layout.size.width, y - layout.size.height / 2f),
            )
        }
        val title = measurer.measure(unitLabel, labelStyle)
        drawText(title, topLeft = Offset(left + 4f, top - title.size.height - 2f))
    }

    panelGrid(wTop, wBottom, wMax, 3, "g")
    panelGrid(pTop, pBottom, pMax, 3, "bar")

    // ---- x axis ----------------------------------------------------------
    val tickStep = when {
        tMax <= 12f -> 2f
        tMax <= 30f -> 5f
        tMax <= 70f -> 10f
        else -> 20f
    }
    var t = 0f
    while (t <= tMax + 0.01f) {
        val px = x(t)
        drawLine(colors.grid, Offset(px, wTop), Offset(px, pBottom), strokeWidth = 1f)
        val layout = measurer.measure("${t.toInt()}", labelStyle)
        drawText(layout, topLeft = Offset(px - layout.size.width / 2f, pBottom + 4f))
        t += tickStep
    }
    val sLabel = measurer.measure("s", labelStyle)
    drawText(sLabel, topLeft = Offset(left + plotW - sLabel.size.width, pBottom + 4f))

    val dash = PathEffect.dashPathEffect(floatArrayOf(8f, 8f), 0f)

    // ---- reference marks --------------------------------------------------
    goalWeight?.let { g ->
        if (g in 0f..wMax) {
            drawLine(
                colors.reference, Offset(left, yw(g)), Offset(left + plotW, yw(g)),
                strokeWidth = 1.5f, pathEffect = dash,
            )
            val layout = measurer.measure("goal ${fmt(g, 0)} g", labelStyle)
            drawText(layout, topLeft = Offset(left + plotW - layout.size.width, yw(g) - layout.size.height - 2f))
        }
    }
    expectedEnd?.let { e ->
        if (e > 0f && e <= tMax) {
            drawLine(
                colors.reference, Offset(x(e), wTop), Offset(x(e), pBottom),
                strokeWidth = 1.5f, pathEffect = dash,
            )
        }
    }
    // The machine's pressure profile, as a step reference under the measurement.
    profile?.byTime?.takeIf { it.isNotEmpty() }?.sortedBy { it.first }?.let { goals ->
        goals.forEachIndexed { i, (time, bar) ->
            val xStart = x(time.coerceIn(0f, tMax))
            val xEnd = if (i + 1 < goals.size) x(goals[i + 1].first.coerceIn(0f, tMax)) else left + plotW
            val py = yp(bar.coerceIn(0f, pMax))
            drawLine(
                colors.reference, Offset(xStart, py), Offset(xEnd, py),
                strokeWidth = 1.5f, pathEffect = dash,
            )
        }
    }

    // ---- series ----------------------------------------------------------
    fun series(values: (Sample) -> Float, toY: (Float) -> Float, color: Color) {
        val path = Path()
        samples.forEachIndexed { i, s ->
            val px = x(s.t)
            val py = toY(values(s))
            if (i == 0) path.moveTo(px, py) else path.lineTo(px, py)
        }
        drawPath(path, color, style = Stroke(width = 2.dp.toPx()))
    }

    series({ it.w }, ::yw, colors.weight)
    series({ it.p }, ::yp, colors.pressure)

    // ---- scrubber --------------------------------------------------------
    scrub?.let { s ->
        val px = x(s.t)
        drawLine(colors.axisText, Offset(px, wTop), Offset(px, pBottom), strokeWidth = 1.5f)
        listOf(yw(s.w) to colors.weight, yp(s.p) to colors.pressure).forEach { (py, c) ->
            drawCircle(colors.surface, radius = 6.dp.toPx(), center = Offset(px, py))
            drawCircle(c, radius = 4.dp.toPx(), center = Offset(px, py))
        }
    }
}

/** Compact weight curve for list rows — no axes, no interaction. */
@Composable
fun ShotSparkline(samples: List<Sample>, modifier: Modifier = Modifier) {
    val colors = viz
    Canvas(modifier) {
        if (samples.size < 2) return@Canvas
        val tMax = max(samples.last().t, 1f)
        val wMax = max(samples.maxOf { it.w }, 1f)
        val path = Path()
        samples.forEachIndexed { i, s ->
            val px = (s.t / tMax) * size.width
            val py = size.height - (s.w / wMax) * size.height
            if (i == 0) path.moveTo(px, py) else path.lineTo(px, py)
        }
        drawPath(path, colors.weight, style = Stroke(width = 2.dp.toPx()))
    }
}
