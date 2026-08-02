package de.fmau.shotcontrol.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput

/**
 * The dark panel state: a black surface over everything, with the window
 * brightness pinned to zero (see MainActivity). Any touch brings it back.
 *
 * Nothing is drawn at all — no clock, no logo — because the whole point is that
 * the machine looks like a machine when it is not in use.
 */
@Composable
fun IdleOverlay(onWake: () -> Unit) {
    Box(
        Modifier
            .fillMaxSize()
            .background(Color.Black)
            .pointerInput(Unit) {
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    onWake()
                }
            }
    )
}
