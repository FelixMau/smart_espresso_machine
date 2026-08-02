package de.fmau.shotcontrol.data

import org.json.JSONArray
import org.json.JSONObject

/** One sample of the extraction curve. */
data class Sample(
    val t: Float,   // seconds since shot start
    val w: Float,   // grams on the scale
    val p: Float,   // bar
)

/** Snapshot of /api/status. */
data class MachineStatus(
    val brewing: Boolean = false,
    val shotId: Int = 0,
    val weight: Float = 0f,
    val pressure: Float = 0f,
    val shotTime: Float = 0f,
    val expectedEnd: Float = 0f,
    val goalWeight: Float = 36f,
    val weightOffset: Float = 1.5f,
    val goalPressure: Float = 0f,
    val pumpPwm: Int = 0,
    val datapoints: Int = 0,
    val scaleConnected: Boolean = false,
    val lastEnd: String = "UNDEF",
    val lastDuration: Float = 0f,
    val finalWeight: Float = 0f,
    val rssi: Int = 0,
) {
    companion object {
        fun fromJson(o: JSONObject) = MachineStatus(
            brewing = o.optBoolean("brewing"),
            shotId = o.optInt("shotId"),
            weight = o.optDouble("weight", 0.0).toFloat(),
            pressure = o.optDouble("pressure", 0.0).toFloat(),
            shotTime = o.optDouble("shotTime", 0.0).toFloat(),
            expectedEnd = o.optDouble("expectedEnd", 0.0).toFloat(),
            goalWeight = o.optDouble("goalWeight", 36.0).toFloat(),
            weightOffset = o.optDouble("weightOffset", 0.0).toFloat(),
            goalPressure = o.optDouble("goalPressure", 0.0).toFloat(),
            pumpPwm = o.optInt("pumpPwm"),
            datapoints = o.optInt("datapoints"),
            scaleConnected = o.optBoolean("scaleConnected"),
            lastEnd = o.optString("lastEnd", "UNDEF"),
            lastDuration = o.optDouble("lastDuration", 0.0).toFloat(),
            finalWeight = o.optDouble("finalWeight", 0.0).toFloat(),
            rssi = o.optInt("rssi"),
        )
    }
}

/** Result of /api/shot — the recorded curve, optionally only the tail (`from`). */
data class ShotCurve(
    val shotId: Int,
    val brewing: Boolean,
    val from: Int,
    val total: Int,
    val goalWeight: Float,
    val weightOffset: Float,
    val duration: Float,
    val finalWeight: Float,
    val end: String,
    val points: List<Sample>,
) {
    companion object {
        fun fromJson(o: JSONObject): ShotCurve {
            val arr = o.optJSONArray("points") ?: JSONArray()
            val points = ArrayList<Sample>(arr.length())
            for (i in 0 until arr.length()) {
                val p = arr.optJSONArray(i) ?: continue
                points.add(
                    Sample(
                        p.optDouble(0, 0.0).toFloat(),
                        p.optDouble(1, 0.0).toFloat(),
                        p.optDouble(2, 0.0).toFloat(),
                    )
                )
            }
            return ShotCurve(
                shotId = o.optInt("shotId"),
                brewing = o.optBoolean("brewing"),
                from = o.optInt("from"),
                total = o.optInt("total"),
                goalWeight = o.optDouble("goalWeight", 36.0).toFloat(),
                weightOffset = o.optDouble("weightOffset", 0.0).toFloat(),
                duration = o.optDouble("duration", 0.0).toFloat(),
                finalWeight = o.optDouble("finalWeight", 0.0).toFloat(),
                end = o.optString("end", "UNDEF"),
                points = points,
            )
        }
    }
}

/** The machine's pressure profile: (time, bar) pairs, plus (time-left, bar) overrides. */
data class PressureProfile(
    val byTime: List<Pair<Float, Float>> = emptyList(),
    val byTimeLeft: List<Pair<Float, Float>> = emptyList(),
) {
    /** Encodes back into the machine's wire format; time-left goals go in as negative times. */
    fun toTimesParam(): String =
        (byTime.map { it.first } + byTimeLeft.map { -it.first }).joinToString(",") { fmt(it) }

    fun toPressuresParam(): String =
        (byTime.map { it.second } + byTimeLeft.map { it.second }).joinToString(",") { fmt(it) }

    private fun fmt(v: Float) = if (v == v.toInt().toFloat()) v.toInt().toString() else v.toString()

    companion object {
        fun fromJson(o: JSONObject): PressureProfile {
            fun pairs(name: String): List<Pair<Float, Float>> {
                val arr = o.optJSONArray(name) ?: return emptyList()
                return (0 until arr.length()).mapNotNull { i ->
                    val p = arr.optJSONArray(i) ?: return@mapNotNull null
                    p.optDouble(0, 0.0).toFloat() to p.optDouble(1, 0.0).toFloat()
                }
            }
            return PressureProfile(pairs("byTime"), pairs("byTimeLeft"))
        }
    }
}

/** A shot as kept in the phone's journal. */
data class StoredShot(
    val id: String,               // file name stem, unique
    val shotId: Int,              // the machine's shot counter (resets on reboot)
    val startedAt: Long,          // epoch millis of shot start
    val durationS: Float,
    val finalWeightG: Float,
    val goalWeightG: Float,
    val endReason: String,
    val samples: List<Sample>,
    val note: String = "",
    val rating: Int = 0,          // 0 = unrated, else 1..5
) {
    val peakPressure: Float get() = samples.maxOfOrNull { it.p } ?: 0f

    /** Grams per second over the part of the shot where coffee actually flows. */
    val avgFlow: Float get() = if (durationS > 0f) finalWeightG / durationS else 0f

    /** Time until the first gram hit the cup — the classic "first drips" marker. */
    val firstDripS: Float? get() = samples.firstOrNull { it.w >= 1f }?.t

    fun toJson(): JSONObject {
        val o = JSONObject()
        o.put("id", id)
        o.put("shotId", shotId)
        o.put("startedAt", startedAt)
        o.put("durationS", durationS.toDouble())
        o.put("finalWeightG", finalWeightG.toDouble())
        o.put("goalWeightG", goalWeightG.toDouble())
        o.put("endReason", endReason)
        o.put("note", note)
        o.put("rating", rating)
        val arr = JSONArray()
        samples.forEach { s ->
            arr.put(JSONArray().put(round2(s.t)).put(round2(s.w)).put(round2(s.p)))
        }
        o.put("samples", arr)
        return o
    }

    private fun round2(v: Float) = Math.round(v * 100f) / 100.0

    companion object {
        fun fromJson(o: JSONObject): StoredShot {
            val arr = o.optJSONArray("samples") ?: JSONArray()
            val samples = ArrayList<Sample>(arr.length())
            for (i in 0 until arr.length()) {
                val p = arr.optJSONArray(i) ?: continue
                samples.add(
                    Sample(
                        p.optDouble(0, 0.0).toFloat(),
                        p.optDouble(1, 0.0).toFloat(),
                        p.optDouble(2, 0.0).toFloat(),
                    )
                )
            }
            return StoredShot(
                id = o.optString("id"),
                shotId = o.optInt("shotId"),
                startedAt = o.optLong("startedAt"),
                durationS = o.optDouble("durationS", 0.0).toFloat(),
                finalWeightG = o.optDouble("finalWeightG", 0.0).toFloat(),
                goalWeightG = o.optDouble("goalWeightG", 0.0).toFloat(),
                endReason = o.optString("endReason", "UNDEF"),
                samples = samples,
                note = o.optString("note", ""),
                rating = o.optInt("rating", 0),
            )
        }
    }
}
