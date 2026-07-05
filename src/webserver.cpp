#include "webserver.h"

#include <ArduinoJson.h>
#include <EEPROM.h>

#include "WiFi.h"
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"

// secrets.h defines 'ssid' and 'password' as macros; it must come after the
// WiFi headers or it clobbers their parameter names
#include <secrets.h>
#include "dashboard.h"
#include "debug.h"
#include "shot_history.h"
#include "shot_stopper.h"

#define WIFI_CONNECT_TIMEOUT_MS 15000

bool wifiConnected = false;
bool serverStarted = false;

volatile bool webStartRequest = false;
volatile bool webStopRequest = false;
volatile bool webResetRequest = false;

static AsyncWebServer server(80);

// PID controller to monitor/tune, set by initializeServer()
static PIDController* webPid = nullptr;

bool initializeWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  DEBUG_STARTUP_PRINT("Connecting to WiFi '%s'...", ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  wifiConnected = (WiFi.status() == WL_CONNECTED);
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
        EEPROM.write(WEIGHT_ADDR, (uint8_t)goalWeight);
        EEPROM.commit();
      }
    }
    req->send(200, "text/plain", "OK");
  });

  server.on("/set_weight_offset", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("value")) {
      float offset = req->getParam("value")->value().toFloat();
      if (offset >= 0 && offset <= MAX_OFFSET) {
        shot.weightOffset = offset;
        EEPROM.write(OFFSET_ADDR, (uint8_t)(offset * 10));
        EEPROM.commit();
      }
    }
    req->send(200, "text/plain", "OK");
  });

  // Pressure profile: comma-separated times and pressures. Positive time =
  // seconds from shot start; negative time = seconds left until expected end.
  server.on("/set_pressure_profile", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("times") && req->hasParam("pressures")) {
      String times = req->getParam("times")->value();
      String pressures = req->getParam("pressures")->value();

      // Parse into locals first, then publish counts last, so the control
      // task never sees a half-written profile
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
    }
    req->send(200, "text/plain", "OK");
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
