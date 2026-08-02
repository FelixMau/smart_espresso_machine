#include "webserver.h"

#include <ArduinoJson.h>

#include "WiFi.h"
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"

// secrets.h defines 'ssid' and 'password' as macros; it must come after the
// WiFi headers or it clobbers their parameter names
#include <secrets.h>
#include "cleaning_cycle.h"
#include "dashboard.h"
#include "debug.h"
#include "settings.h"
#include "shot_history.h"
#include "shot_stopper.h"

#define WIFI_CONNECT_TIMEOUT_MS 15000

bool wifiConnected = false;
bool serverStarted = false;

volatile bool webStartRequest = false;
volatile bool webStopRequest = false;
volatile bool webResetRequest = false;
volatile bool webRebootRequest = false;

static AsyncWebServer server(80);

// PID controller to monitor/tune, set by initializeServer()
static PIDController* webPid = nullptr;

// Try one set of credentials with a bounded wait; returns the WiFi status
static bool tryWifi(const char* trySsid, const char* tryPass) {
  WiFi.begin(trySsid, tryPass);
  DEBUG_STARTUP_PRINT("Connecting to WiFi '%s'...", trySsid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool initializeWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  // Credentials stored via /set_wifi win; fall back to the compile-time
  // secrets.h ones if none are stored OR the stored ones fail to connect,
  // so a typo'd password can't brick the dashboard permanently
  bool storedCreds = settings.wifiSsid[0] != '\0';
  wifiConnected = tryWifi(storedCreds ? settings.wifiSsid : ssid,
                          storedCreds ? settings.wifiPassword : password);
  if (!wifiConnected && storedCreds && strcmp(settings.wifiSsid, ssid) != 0) {
    DEBUG_STARTUP_PRINT("Stored WiFi credentials failed - trying compile-time secrets.h");
    wifiConnected = tryWifi(ssid, password);
  }
  if (wifiConnected) {
    DEBUG_STARTUP_PRINT("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
    // SNTP for real shot timestamps in the Beanconqueror export; syncs in the
    // background and is harmless if the NTP server is unreachable (timestamps
    // then stay near the 1970 epoch)
    configTime(0, 0, "pool.ntp.org");
  } else {
    DEBUG_STARTUP_PRINT("WiFi connection failed after %d ms - web dashboard disabled",
                        WIFI_CONNECT_TIMEOUT_MS);
  }
  return wifiConnected;
}

// Parses two comma-separated lists into the pressure profile. Positive time =
// seconds from shot start; negative time = seconds left until the expected end.
// Shared by /set_pressure_profile (dashboard) and /api/set_pressure_profile
// (phone app), which differ only in how they answer.
static void applyPressureProfile(String times, String pressures) {
  // Parse into locals first, then publish counts last, so the control task
  // never sees a half-written profile
  PressureGoalByTime byTime[MAX_PRESSURE_GOALS];
  PressureGoalByTimeLeft byTimeLeft[MAX_PRESSURE_GOALS];
  int numByTime = 0, numByTimeLeft = 0;

  while (times.length() && pressures.length() &&
         (numByTime < MAX_PRESSURE_GOALS || numByTimeLeft < MAX_PRESSURE_GOALS)) {
    int timeIdx = times.indexOf(',');
    String timeStr = timeIdx >= 0 ? times.substring(0, timeIdx) : times;
    times = timeIdx >= 0 ? times.substring(timeIdx + 1) : "";
    int pressureIdx = pressures.indexOf(',');
    String pressureStr = pressureIdx >= 0 ? pressures.substring(0, pressureIdx) : pressures;
    pressures = pressureIdx >= 0 ? pressures.substring(pressureIdx + 1) : "";

    float timeVal = timeStr.toFloat();
    float pressureVal = pressureStr.toFloat();

    if (timeVal < 0 && numByTimeLeft < MAX_PRESSURE_GOALS) {
      byTimeLeft[numByTimeLeft].timeLeftS = -timeVal;
      byTimeLeft[numByTimeLeft].pressure = pressureVal;
      numByTimeLeft++;
    } else if (timeVal >= 0 && numByTime < MAX_PRESSURE_GOALS) {
      byTime[numByTime].timeS = timeVal;
      byTime[numByTime].pressure = pressureVal;
      numByTime++;
    }
  }

  shot.numPressureGoalsByTime = 0;
  shot.numPressureGoalsByTimeLeft = 0;
  memcpy(shot.pressureGoalByTime, byTime, sizeof(byTime));
  memcpy(shot.pressureGoalByTimeLeft, byTimeLeft, sizeof(byTimeLeft));
  shot.numPressureGoalsByTime = numByTime;
  shot.numPressureGoalsByTimeLeft = numByTimeLeft;
  DEBUG_SHOT_PRINT("Pressure profile set via web: %d by-time, %d by-time-left goals",
                   numByTime, numByTimeLeft);
  settingsSave();
}

// Reads a parameter from the query string or, failing that, a form body -
// the phone app posts some of its commands.
static bool paramValue(AsyncWebServerRequest* req, const char* name, String& out) {
  if (req->hasParam(name)) {
    out = req->getParam(name)->value();
    return true;
  }
  if (req->hasParam(name, true)) {
    out = req->getParam(name, true)->value();
    return true;
  }
  return false;
}

void initializeServer(PIDController* pid) {
  webPid = pid;

  // Permissive CORS so Beanconqueror's webview can reach the /api endpoints;
  // harmless for the dashboard (LAN-only, no credentials anywhere)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  // Dashboard page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", DASHBOARD_HTML);
  });

  // Single JSON state endpoint: everything the dashboard polls, in one request
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["brewing"] = shot.brewing;
    doc["scaleConnected"] = (bool)scaleConnected;
    doc["shotTimer"] = shot.shotTimer;
    doc["expectedEnd"] = shot.expectedEndS;
    doc["weight"] = (float)currentWeight;
    doc["goalWeight"] = shot.goalWeight;
    doc["weightOffset"] = shot.weightOffset;
    doc["pressure"] = shot.pressure;
    doc["goalPressure"] = shot.currentGoalPressure;
    doc["pumpPwm"] = shot.pumpPwm;
    doc["pumpFlow"] = shot.pumpFlow;
    // SSID only, never the password; empty = compile-time secrets.h in use
    doc["wifiSsid"] = settings.wifiSsid;

    // Cleaning cycle status + live config (for the dashboard editors)
    JsonObject cl = doc["cleaning"].to<JsonObject>();
    cl["active"] = cleaningActive();
    cl["phase"] = cleaningPhaseName();
    cl["state"] = cleaningStateName();
    cl["cycle"] = cleaningCurrentCycle();
    cl["cycles"] = cleaningConfig.cyclesPerPhase;
    cl["elapsed"] = cleaningStateElapsedS();
    cl["lastFillPeak"] = cleaningLastFillPeakBar();
    cl["lastFillReachedMax"] = cleaningLastFillReachedMax();
    cl["maxPressure"] = cleaningConfig.maxPressureBar;
    cl["holdS"] = cleaningConfig.holdS;
    cl["pauseS"] = cleaningConfig.pauseS;
    cl["soakS"] = cleaningConfig.soakS;

    JsonObject pid = doc["pid"].to<JsonObject>();
    if (webPid) {
      pid["kp"] = webPid->kp;
      pid["ki"] = webPid->ki;
      pid["kd"] = webPid->kd;
      pid["p"] = webPid->getPTerm();
      pid["i"] = webPid->getITerm();
      pid["d"] = webPid->getDTerm();
      pid["out"] = webPid->getOutput();
    }

    // Pressure profile: by-time goals as positive times, by-time-left goals
    // as negative times (same convention as /set_pressure_profile input)
    JsonArray times = doc["profileTimes"].to<JsonArray>();
    JsonArray pressures = doc["profilePressures"].to<JsonArray>();
    for (int i = 0; i < shot.numPressureGoalsByTime; i++) {
      times.add(shot.pressureGoalByTime[i].timeS);
      pressures.add(shot.pressureGoalByTime[i].pressure);
    }
    for (int i = 0; i < shot.numPressureGoalsByTimeLeft; i++) {
      times.add(-shot.pressureGoalByTimeLeft[i].timeLeftS);
      pressures.add(shot.pressureGoalByTimeLeft[i].pressure);
    }

    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  // Start/stop: request only, executed by the control task
  server.on("/start_shot", HTTP_GET, [](AsyncWebServerRequest* req) {
    webStartRequest = true;
    req->send(200, "text/plain", "OK");
  });
  server.on("/stop_shot", HTTP_GET, [](AsyncWebServerRequest* req) {
    webStopRequest = true;
    req->send(200, "text/plain", "OK");
  });
  // Reset: end the shot on ESP + scale only, machine button untouched
  server.on("/reset_shot", HTTP_GET, [](AsyncWebServerRequest* req) {
    webResetRequest = true;
    req->send(200, "text/plain", "OK");
  });

  // Cleaning cycle (automated detergent backflush): request flags only,
  // executed by the control task via cleaningUpdate()
  server.on("/start_cleaning", HTTP_GET, [](AsyncWebServerRequest* req) {
    cleaningStartRequest = true;
    req->send(200, "text/plain", "OK");
  });
  server.on("/continue_cleaning", HTTP_GET, [](AsyncWebServerRequest* req) {
    cleaningContinueRequest = true;
    req->send(200, "text/plain", "OK");
  });
  server.on("/stop_cleaning", HTTP_GET, [](AsyncWebServerRequest* req) {
    cleaningStopRequest = true;
    req->send(200, "text/plain", "OK");
  });

  // Cleaning parameters; each is optional and range-checked, persisted to
  // EEPROM via the settings blob
  server.on("/set_cleaning", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("max_pressure")) {
      float v = req->getParam("max_pressure")->value().toFloat();
      if (v >= 4 && v <= 12) cleaningConfig.maxPressureBar = v;
    }
    if (req->hasParam("cycles")) {
      int v = req->getParam("cycles")->value().toInt();
      if (v >= 1 && v <= 10) cleaningConfig.cyclesPerPhase = v;
    }
    if (req->hasParam("hold_s")) {
      float v = req->getParam("hold_s")->value().toFloat();
      if (v >= 0 && v <= 30) cleaningConfig.holdS = v;
    }
    if (req->hasParam("pause_s")) {
      float v = req->getParam("pause_s")->value().toFloat();
      if (v >= 2 && v <= 60) cleaningConfig.pauseS = v;
    }
    if (req->hasParam("soak_s")) {
      float v = req->getParam("soak_s")->value().toFloat();
      if (v >= 0 && v <= 600) cleaningConfig.soakS = v;
    }
    DEBUG_CLEANING_PRINT("Cleaning config set via web: max %.1f bar, %d cycles, hold %.0f s, pause %.0f s, soak %.0f s",
                         cleaningConfig.maxPressureBar, cleaningConfig.cyclesPerPhase,
                         cleaningConfig.holdS, cleaningConfig.pauseS, cleaningConfig.soakS);
    settingsSave();
    req->send(200, "text/plain", "OK");
  });

  // Live PID tuning; each gain is optional
  server.on("/set_pid", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (webPid) {
      if (req->hasParam("kp")) webPid->kp = req->getParam("kp")->value().toFloat();
      if (req->hasParam("ki")) webPid->ki = req->getParam("ki")->value().toFloat();
      if (req->hasParam("kd")) webPid->kd = req->getParam("kd")->value().toFloat();
      DEBUG_SHOT_PRINT("PID gains set via web: Kp=%.1f Ki=%.2f Kd=%.1f",
                       webPid->kp, webPid->ki, webPid->kd);
    }
    req->send(200, "text/plain", "OK");
  });

  server.on("/set_goal_weight", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("value")) {
      float goalWeight = req->getParam("value")->value().toFloat();
      if (goalWeight >= 10 && goalWeight <= 200) {
        shot.goalWeight = goalWeight;
        settingsSave();
      }
    }
    req->send(200, "text/plain", "OK");
  });

  server.on("/set_weight_offset", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("value")) {
      float offset = req->getParam("value")->value().toFloat();
      if (offset >= 0 && offset <= MAX_OFFSET) {
        shot.weightOffset = offset;
        settingsSave();
      }
    }
    req->send(200, "text/plain", "OK");
  });

  // Pressure profile: comma-separated times and pressures. Positive time =
  // seconds from shot start; negative time = seconds left until expected end.
  server.on("/set_pressure_profile", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("times") && req->hasParam("pressures")) {
      applyPressureProfile(req->getParam("times")->value(),
                           req->getParam("pressures")->value());
    }
    req->send(200, "text/plain", "OK");
  });

  // WiFi credentials: stored to EEPROM, used on next boot (boot falls back
  // to the compile-time secrets.h credentials if the stored ones fail, so a
  // typo can't lock the dashboard out). Empty ssid reverts to secrets.h.
  server.on("/set_wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid")) {
      req->send(400, "text/plain", "missing ssid");
      return;
    }
    String newSsid = req->getParam("ssid")->value();
    String newPass = req->hasParam("pass") ? req->getParam("pass")->value() : "";
    if (newSsid.length() > 32 || newPass.length() > 64) {
      req->send(400, "text/plain", "ssid max 32 chars, password max 64");
      return;
    }
    settingsSetWifi(newSsid.c_str(), newPass.c_str());
    req->send(200, "text/plain",
              newSsid.length() ? "OK - stored, takes effect on next boot (use /reboot)"
                               : "OK - reverted to compile-time credentials on next boot");
  });

  // Reboot (e.g. to apply new WiFi credentials); executed by loop() so the
  // response gets out first. Refused while the machine is doing anything.
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (shot.brewing || cleaningActive()) {
      req->send(409, "text/plain", "busy: shot or cleaning in progress");
      return;
    }
    webRebootRequest = true;
    req->send(200, "text/plain", "OK - rebooting");
  });

  // Shot history list with per-shot key findings (newest first)
  server.on("/shots", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc["shots"].to<JsonArray>();
    if (!shotHistoryLock || xSemaphoreTake(shotHistoryLock, pdMS_TO_TICKS(100)) == pdTRUE) {
      for (int i = 0; i < shotHistoryCount; i++) {
        // Newest first: walk backwards from the last written slot
        int idx = (shotHistoryWriteIdx - 1 - i + HISTORY_MAX_SHOTS * 2) % HISTORY_MAX_SHOTS;
        ShotRecord& rec = shotHistory[idx];
        JsonObject o = arr.add<JsonObject>();
        o["id"] = rec.id;
        o["duration"] = rec.durationS;
        o["finalWeight"] = rec.finalWeight;
        o["peakPressure"] = rec.peakPressure;
        o["endReason"] = endReasonName((EndType)rec.endReason);
        o["points"] = rec.numPoints;
      }
      if (shotHistoryLock) xSemaphoreGive(shotHistoryLock);
    }
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  // Downsampled trajectory of one recorded shot
  server.on("/shot", HTTP_GET, [](AsyncWebServerRequest* req) {
    uint32_t id = req->hasParam("id") ? (uint32_t)req->getParam("id")->value().toInt() : 0;
    JsonDocument doc;
    bool found = false;
    if (!shotHistoryLock || xSemaphoreTake(shotHistoryLock, pdMS_TO_TICKS(100)) == pdTRUE) {
      for (int i = 0; i < shotHistoryCount; i++) {
        ShotRecord& rec = shotHistory[i];
        if (rec.id == id) {
          doc["id"] = rec.id;
          JsonArray t = doc["t"].to<JsonArray>();
          JsonArray w = doc["w"].to<JsonArray>();
          for (int j = 0; j < rec.numPoints; j++) {
            t.add(rec.timeS[j]);
            w.add(rec.weight[j]);
          }
          found = true;
          break;
        }
      }
      if (shotHistoryLock) xSemaphoreGive(shotHistoryLock);
    }
    if (!found) {
      req->send(404, "text/plain", "shot not found");
      return;
    }
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  // ==========================================================================
  // PHONE APP API (android/, MachineApi.kt)
  // ==========================================================================
  // The app mirrors a shot while it is being pulled, so it asks for the live
  // sample arrays with ?from= and only fetches what it does not have yet -
  // /shots and /shot serve the finished, downsampled history instead and are
  // no substitute. Registered before the /api/shots/* wildcard below.

  // All live values in one request.
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["brewing"] = shot.brewing;
    // While brewing this is the id the shot will be filed under; afterwards
    // it is that same id, so the app sees one stable number per shot
    doc["shotId"] = shot.brewing ? shotHistoryNextId() : (shotHistoryNextId() - 1);
    doc["weight"] = (float)currentWeight;
    doc["pressure"] = shot.pressure;
    doc["shotTime"] = shot.shotTimer;
    doc["expectedEnd"] = shot.expectedEndS;
    doc["goalWeight"] = shot.goalWeight;
    doc["weightOffset"] = shot.weightOffset;
    doc["goalPressure"] = shot.currentGoalPressure;
    doc["pumpPwm"] = shot.pumpPwm;
    doc["pumpFlow"] = shot.pumpFlow;
    doc["datapoints"] = shot.datapoints;
    doc["scaleConnected"] = (bool)scaleConnected;
    doc["uptimeMs"] = millis();
    doc["rssi"] = WiFi.RSSI();

    // Summary of the last finished shot, taken from the newest history slot
    doc["lastEnd"] = "UNDEF";
    doc["lastDuration"] = 0.0f;
    doc["finalWeight"] = 0.0f;
    if (shotHistoryCount > 0 &&
        (!shotHistoryLock || xSemaphoreTake(shotHistoryLock, pdMS_TO_TICKS(100)) == pdTRUE)) {
      int idx = (shotHistoryWriteIdx - 1 + HISTORY_MAX_SHOTS) % HISTORY_MAX_SHOTS;
      ShotRecord& rec = shotHistory[idx];
      doc["lastEnd"] = endReasonName((EndType)rec.endReason);
      doc["lastDuration"] = rec.durationS;
      doc["finalWeight"] = rec.finalWeight;
      if (shotHistoryLock) xSemaphoreGive(shotHistoryLock);
    }

    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  // Full (or incremental, via ?from=<index>) sample curve of the current or
  // last shot. Streamed so a 1000-point shot never needs a multi-kB String.
  // shot.datapoints only ever grows until the *next* shot starts, so reading
  // [from, total) without a lock cannot tear.
  server.on("/api/shot", HTTP_GET, [](AsyncWebServerRequest* req) {
    String v;
    int from = paramValue(req, "from", v) ? v.toInt() : 0;
    const int total = shot.datapoints;
    if (from < 0) from = 0;
    if (from > total) from = total;

    AsyncResponseStream* res = req->beginResponseStream("application/json");
    res->printf("{\"shotId\":%lu,\"brewing\":%s,\"from\":%d,\"total\":%d",
                (unsigned long)(shot.brewing ? shotHistoryNextId() : shotHistoryNextId() - 1),
                shot.brewing ? "true" : "false", from, total);
    res->printf(",\"goalWeight\":%.1f,\"weightOffset\":%.1f", shot.goalWeight, shot.weightOffset);
    res->printf(",\"duration\":%.2f,\"finalWeight\":%.2f,\"end\":\"%s\"",
                shot.endS, total > 0 ? shot.weight[total - 1] : 0.0f, endReasonName(shot.end));
    res->print(",\"points\":[");
    for (int i = from; i < total; i++) {
      if (i > from) res->print(',');
      res->printf("[%.2f,%.2f,%.2f]", shot.timeS[i], shot.weight[i], shot.pressureTrace[i]);
    }
    res->print("]}");
    req->send(res);
  });

  // The pressure profile in the shape the app's editor expects
  server.on("/api/profile", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray byTime = doc["byTime"].to<JsonArray>();
    for (int i = 0; i < shot.numPressureGoalsByTime; i++) {
      JsonArray p = byTime.add<JsonArray>();
      p.add(shot.pressureGoalByTime[i].timeS);
      p.add(shot.pressureGoalByTime[i].pressure);
    }
    JsonArray byTimeLeft = doc["byTimeLeft"].to<JsonArray>();
    for (int i = 0; i < shot.numPressureGoalsByTimeLeft; i++) {
      JsonArray p = byTimeLeft.add<JsonArray>();
      p.add(shot.pressureGoalByTimeLeft[i].timeLeftS);
      p.add(shot.pressureGoalByTimeLeft[i].pressure);
    }
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  // Brew control - like /start_shot and /stop_shot, deferred to controlTask
  server.on("/api/start", HTTP_ANY, [](AsyncWebServerRequest* req) {
    webStartRequest = true;
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/stop", HTTP_ANY, [](AsyncWebServerRequest* req) {
    webStopRequest = true;
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/set_goal_weight", HTTP_ANY, [](AsyncWebServerRequest* req) {
    String v;
    if (paramValue(req, "value", v)) {
      float goalWeight = v.toFloat();
      if (goalWeight >= 10 && goalWeight <= 200) {
        shot.goalWeight = goalWeight;
        settingsSave();
        req->send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
    req->send(400, "application/json", "{\"error\":\"value must be 10-200 g\"}");
  });

  server.on("/api/set_weight_offset", HTTP_ANY, [](AsyncWebServerRequest* req) {
    String v;
    if (paramValue(req, "value", v)) {
      float offset = v.toFloat();
      if (offset >= 0 && offset <= MAX_OFFSET) {
        shot.weightOffset = offset;
        settingsSave();
        req->send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
    req->send(400, "application/json", "{\"error\":\"value out of range\"}");
  });

  server.on("/api/set_pressure_profile", HTTP_ANY, [](AsyncWebServerRequest* req) {
    String times, pressures;
    if (!paramValue(req, "times", times) || !paramValue(req, "pressures", pressures)) {
      req->send(400, "application/json", "{\"error\":\"times and pressures required\"}");
      return;
    }
    applyPressureProfile(times, pressures);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // ==========================================================================
  // BEANCONQUEROR INTEGRATION (Gaggiuino REST API emulation)
  // ==========================================================================
  // Beanconqueror reaches WiFi machines through its preparationDevice layer;
  // we speak the Gaggiuino dialect so the app needs no changes: add the ESP's
  // LAN IP as a GAGGIUINO preparation device. BQ's Gaggiuino client has no
  // polling loop - it imports the full shot graph once after a brew.
  // Contract (gaggiuinoDevice.ts): every datapoint value is an integer of
  // real value x 10; BQ divides by 10 on import.

  // Connection/validity check: BQ only checks for HTTP 200, body is ignored
  server.on("/api/system/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // BQ reads responseJSON[0].lastShotId; id 0 = no shots yet (fetch will 404)
  server.on("/api/shots/latest", HTTP_GET, [](AsyncWebServerRequest* req) {
    uint32_t lastId = 0;
    if (!shotHistoryLock || xSemaphoreTake(shotHistoryLock, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (shotHistoryCount > 0) {
        int idx = (shotHistoryWriteIdx - 1 + HISTORY_MAX_SHOTS) % HISTORY_MAX_SHOTS;
        lastId = shotHistory[idx].id;
      }
      if (shotHistoryLock) xSemaphoreGive(shotHistoryLock);
    }
    req->send(200, "application/json", "[{\"lastShotId\":" + String(lastId) + "}]");
  });

  // /api/shots/{id} - must be registered AFTER /api/shots/latest: handlers
  // match in registration order and this wildcard would swallow "latest" too
  server.on("/api/shots/*", HTTP_GET, [](AsyncWebServerRequest* req) {
    uint32_t id = (uint32_t)req->url().substring(strlen("/api/shots/")).toInt();
    JsonDocument doc;
    bool found = false;
    if (!shotHistoryLock || xSemaphoreTake(shotHistoryLock, pdMS_TO_TICKS(100)) == pdTRUE) {
      for (int i = 0; i < shotHistoryCount; i++) {
        ShotRecord& rec = shotHistory[i];
        if (rec.id != id) {
          continue;
        }
        doc["id"] = rec.id;
        doc["timestamp"] = rec.timestamp;  // unix seconds (BQ renders via moment.unix)
        // BQ's import modal reads the nested profile.name and silently skips
        // the whole shot if it's missing; profileName alone is not enough
        doc["profileName"] = "Smart Espresso";
        doc["profile"]["name"] = "Smart Espresso";
        JsonObject dp = doc["datapoints"].to<JsonObject>();
        JsonArray t = dp["timeInShot"].to<JsonArray>();
        JsonArray p = dp["pressure"].to<JsonArray>();
        JsonArray f = dp["pumpFlow"].to<JsonArray>();
        JsonArray w = dp["shotWeight"].to<JsonArray>();
        JsonArray temp = dp["temperature"].to<JsonArray>();
        for (int j = 0; j < rec.numPoints; j++) {
          t.add((int)lroundf(rec.timeS[j] * 10));
          p.add((int)lroundf(rec.pressure[j] * 10));
          // No flow sensor: derive flow from the weight trajectory
          // (g/s ~= ml/s for espresso), clamped against scale noise
          float flow = 0;
          if (j > 0) {
            float dt = rec.timeS[j] - rec.timeS[j - 1];
            if (dt > 0) {
              flow = (rec.weight[j] - rec.weight[j - 1]) / dt;
            }
          }
          if (flow < 0) {
            flow = 0;
          }
          f.add((int)lroundf(flow * 10));
          temp.add(0);  // no brew temperature sensor
          w.add((int)lroundf(rec.weight[j] * 10));
        }
        found = true;
        break;
      }
      if (shotHistoryLock) xSemaphoreGive(shotHistoryLock);
    }
    if (!found) {
      req->send(404, "application/json", "{\"error\":\"shot not found\"}");
      return;
    }
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
  });

  server.begin();
  serverStarted = true;
  DEBUG_STARTUP_PRINT("Web server started on port 80");
}
