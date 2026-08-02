package de.fmau.shotcontrol.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

private val Espresso = Color(0xFF6F4E37)
private val EspressoDark = Color(0xFF3E2723)
private val Crema = Color(0xFFE8C39E)

private val LightScheme = lightColorScheme(
    primary = Espresso,
    onPrimary = Color.White,
    primaryContainer = Color(0xFFF3E3D3),
    onPrimaryContainer = EspressoDark,
    secondary = Color(0xFF7A5C43),
    background = Color(0xFFFCFCFB),
    onBackground = Color(0xFF0B0B0B),
    surface = Color(0xFFFCFCFB),
    onSurface = Color(0xFF0B0B0B),
    surfaceVariant = Color(0xFFF0EFEC),
    onSurfaceVariant = Color(0xFF52514E),
    error = Color(0xFFE34948),
)

private val DarkScheme = darkColorScheme(
    primary = Crema,
    onPrimary = Color(0xFF2A1A10),
    primaryContainer = Color(0xFF503422),
    onPrimaryContainer = Color(0xFFF3E3D3),
    secondary = Color(0xFFD3B79C),
    background = Color(0xFF141413),
    onBackground = Color(0xFFFFFFFF),
    surface = Color(0xFF1A1A19),
    onSurface = Color(0xFFFFFFFF),
    surfaceVariant = Color(0xFF383835),
    onSurfaceVariant = Color(0xFFC3C2B7),
    error = Color(0xFFE66767),
)

/**
 * Chart colors, kept out of the Material scheme on purpose: series identity has
 * to stay stable and validated for colour-vision deficiency, independent of the
 * UI accent. Both columns are the same hues stepped for their own surface.
 */
@Immutable
data class VizColors(
    val weight: Color,        // categorical slot 1 (blue)
    val pressure: Color,      // categorical slot 2 (orange)
    val reference: Color,     // goal lines / recessive marks
    val grid: Color,
    val axisText: Color,
    val surface: Color,
) {
    companion object {
        val Light = VizColors(
            weight = Color(0xFF2A78D6),
            pressure = Color(0xFFEB6834),
            reference = Color(0xFF8A8A85),
            grid = Color(0xFFE4E3DF),
            axisText = Color(0xFF52514E),
            surface = Color(0xFFFCFCFB),
        )
        val Dark = VizColors(
            weight = Color(0xFF3987E5),
            pressure = Color(0xFFD95926),
            reference = Color(0xFF8A8A85),
            grid = Color(0xFF2E2E2C),
            axisText = Color(0xFFC3C2B7),
            surface = Color(0xFF1A1A19),
        )
    }
}

val LocalVizColors = staticCompositionLocalOf { VizColors.Light }

val viz: VizColors
    @Composable @ReadOnlyComposable
    get() = LocalVizColors.current

@Composable
fun ShotControlTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    androidx.compose.runtime.CompositionLocalProvider(
        LocalVizColors provides if (darkTheme) VizColors.Dark else VizColors.Light
    ) {
        MaterialTheme(
            colorScheme = if (darkTheme) DarkScheme else LightScheme,
            content = content,
        )
    }
}
