package de.fmau.shotcontrol.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/**
 * Thin client for the ESP32's /api endpoints (see src/webserver.h).
 *
 * Deliberately dependency-free: HttpURLConnection + org.json. The machine is one
 * hop away on the LAN, the payloads are tiny, and a polling app benefits more
 * from short timeouts than from a fat HTTP stack.
 */
class MachineApi(@Volatile var host: String) {

    class ApiException(message: String) : IOException(message)

    private fun baseUrl(): String {
        val h = host.trim().removeSuffix("/")
        return if (h.startsWith("http://") || h.startsWith("https://")) h else "http://$h"
    }

    private suspend fun get(path: String, timeoutMs: Int = DEFAULT_TIMEOUT_MS): String =
        withContext(Dispatchers.IO) {
            val conn = (URL(baseUrl() + path).openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                connectTimeout = timeoutMs
                readTimeout = timeoutMs
                useCaches = false
                setRequestProperty("Connection", "close")  // the ESP has few sockets
                setRequestProperty("Accept", "application/json")
            }
            try {
                val code = conn.responseCode
                val body = (if (code in 200..299) conn.inputStream else conn.errorStream)
                    ?.bufferedReader()?.use { it.readText() }.orEmpty()
                if (code !in 200..299) {
                    val msg = runCatching { JSONObject(body).optString("error") }.getOrNull()
                    throw ApiException(if (msg.isNullOrBlank()) "HTTP $code" else msg)
                }
                body
            } finally {
                conn.disconnect()
            }
        }

    private fun enc(v: String): String = URLEncoder.encode(v, "UTF-8")

    suspend fun status(): MachineStatus = MachineStatus.fromJson(JSONObject(get("/api/status")))

    /** Fetches the curve of the current/last shot; `from` skips samples already held. */
    suspend fun shot(from: Int = 0): ShotCurve =
        ShotCurve.fromJson(JSONObject(get("/api/shot?from=$from", CURVE_TIMEOUT_MS)))

    suspend fun profile(): PressureProfile = PressureProfile.fromJson(JSONObject(get("/api/profile")))

    suspend fun start() { get("/api/start") }

    suspend fun stop() { get("/api/stop") }

    suspend fun setGoalWeight(grams: Float) { get("/api/set_goal_weight?value=$grams") }

    suspend fun setWeightOffset(grams: Float) { get("/api/set_weight_offset?value=$grams") }

    suspend fun setPressureProfile(profile: PressureProfile) {
        get("/api/set_pressure_profile?times=${enc(profile.toTimesParam())}" +
            "&pressures=${enc(profile.toPressuresParam())}")
    }

    private companion object {
        const val DEFAULT_TIMEOUT_MS = 2500
        const val CURVE_TIMEOUT_MS = 6000
    }
}
