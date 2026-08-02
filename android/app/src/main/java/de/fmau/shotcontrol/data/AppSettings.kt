package de.fmau.shotcontrol.data

import android.content.Context

/** Persisted app preferences (machine address, polling, auto-save). */
class AppSettings(context: Context) {

    private val prefs = context.getSharedPreferences("shotcontrol", Context.MODE_PRIVATE)

    var host: String
        get() = prefs.getString(KEY_HOST, DEFAULT_HOST) ?: DEFAULT_HOST
        set(value) = prefs.edit().putString(KEY_HOST, value.trim()).apply()

    /** Poll period while idle; during a shot the app polls as fast as [liveIntervalMs]. */
    var idleIntervalMs: Long
        get() = prefs.getLong(KEY_IDLE_MS, 1500L)
        set(value) = prefs.edit().putLong(KEY_IDLE_MS, value).apply()

    var liveIntervalMs: Long
        get() = prefs.getLong(KEY_LIVE_MS, 300L)
        set(value) = prefs.edit().putLong(KEY_LIVE_MS, value).apply()

    /** Save every finished shot to the journal without asking. */
    var autoSaveShots: Boolean
        get() = prefs.getBoolean(KEY_AUTOSAVE, true)
        set(value) = prefs.edit().putBoolean(KEY_AUTOSAVE, value).apply()

    /** Ignore shots shorter than this (group flushes). Matches MIN_SHOT_DURATION_S. */
    var minSaveDurationS: Float
        get() = prefs.getFloat(KEY_MIN_DURATION, 5f)
        set(value) = prefs.edit().putFloat(KEY_MIN_DURATION, value).apply()

    // --- panel mode (phone mounted on the machine) ---------------------------

    /** Darken the screen when nothing is happening, and keep polling in a service. */
    var kioskEnabled: Boolean
        get() = prefs.getBoolean(KEY_KIOSK, false)
        set(value) = prefs.edit().putBoolean(KEY_KIOSK, value).apply()

    /** [IdleStyle] name; OVERLAY needs no permissions, SCREEN_OFF needs device admin. */
    var idleStyle: String
        get() = prefs.getString(KEY_IDLE_STYLE, "OVERLAY") ?: "OVERLAY"
        set(value) = prefs.edit().putString(KEY_IDLE_STYLE, value).apply()

    /** Minutes of no interaction and no brewing before the panel goes dark. */
    var idleTimeoutMinutes: Int
        get() = prefs.getInt(KEY_IDLE_MINUTES, 5)
        set(value) = prefs.edit().putInt(KEY_IDLE_MINUTES, value).apply()

    /**
     * Poll period while the panel is dark. Slower to save power, but this is also
     * how quickly the screen reacts when the scale wakes up — so not too slow.
     */
    var darkIntervalMs: Long
        get() = prefs.getLong(KEY_DARK_MS, 5000L)
        set(value) = prefs.edit().putLong(KEY_DARK_MS, value).apply()

    private companion object {
        const val DEFAULT_HOST = "espresso.local"
        const val KEY_HOST = "host"
        const val KEY_IDLE_MS = "idle_ms"
        const val KEY_LIVE_MS = "live_ms"
        const val KEY_AUTOSAVE = "autosave"
        const val KEY_MIN_DURATION = "min_duration"
        const val KEY_KIOSK = "kiosk"
        const val KEY_IDLE_STYLE = "idle_style"
        const val KEY_IDLE_MINUTES = "idle_minutes"
        const val KEY_DARK_MS = "dark_ms"
    }
}
