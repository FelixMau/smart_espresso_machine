package de.fmau.shotcontrol.data

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

/**
 * The shot journal: one JSON file per shot in the app's private storage.
 *
 * A file per shot (rather than a database) keeps the whole journal greppable and
 * exportable, and a few hundred espressos a year will never make this slow.
 */
class ShotStore(context: Context) {

    private val dir = File(context.filesDir, "shots").apply { mkdirs() }
    private val exportDir = File(context.cacheDir, "exports").apply { mkdirs() }

    private val _shots = MutableStateFlow<List<StoredShot>>(emptyList())

    /** Newest first. */
    val shots: StateFlow<List<StoredShot>> = _shots.asStateFlow()

    suspend fun load() = withContext(Dispatchers.IO) {
        val loaded = dir.listFiles { f -> f.extension == "json" }
            ?.mapNotNull { f -> runCatching { StoredShot.fromJson(JSONObject(f.readText())) }.getOrNull() }
            ?.sortedByDescending { it.startedAt }
            ?: emptyList()
        _shots.value = loaded
    }

    suspend fun save(shot: StoredShot) = withContext(Dispatchers.IO) {
        File(dir, "${shot.id}.json").writeText(shot.toJson().toString())
        _shots.value = (_shots.value.filterNot { it.id == shot.id } + shot)
            .sortedByDescending { it.startedAt }
    }

    suspend fun delete(id: String) = withContext(Dispatchers.IO) {
        File(dir, "$id.json").delete()
        _shots.value = _shots.value.filterNot { it.id == id }
    }

    fun byId(id: String): StoredShot? = _shots.value.firstOrNull { it.id == id }

    /** Writes one shot's samples as CSV into the cache dir, for sharing. */
    suspend fun exportCsv(shot: StoredShot): File = withContext(Dispatchers.IO) {
        val file = File(exportDir, "shot-${shot.id}.csv")
        file.bufferedWriter().use { out ->
            out.write("# shot ${shot.id}  goal=${shot.goalWeightG}g  final=${shot.finalWeightG}g  ")
            out.write("duration=${shot.durationS}s  end=${shot.endReason}\n")
            out.write("time_s,weight_g,pressure_bar\n")
            shot.samples.forEach { out.write("${it.t},${it.w},${it.p}\n") }
        }
        file
    }

    /** Writes the whole journal as one JSON document, for backup/analysis. */
    suspend fun exportAllJson(): File = withContext(Dispatchers.IO) {
        val file = File(exportDir, "shot-journal.json")
        val arr = org.json.JSONArray()
        _shots.value.forEach { arr.put(it.toJson()) }
        file.writeText(arr.toString())
        file
    }
}
