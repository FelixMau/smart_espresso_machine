// Guard name must NOT be WEBSERVER_H: ESPAsyncWebServer checks that macro to
// detect the core WebServer library and would skip defining HTTP_GET etc.
#ifndef ESPRESSO_WEBSERVER_H
#define ESPRESSO_WEBSERVER_H

// ============================================================================
// LAN WEB DASHBOARD SERVER
// ============================================================================
// Async web server for monitoring and controlling the machine while it brews.
// All HTTP handlers run in the AsyncTCP task, NOT the control task. Handlers
// therefore never touch the scale/BLE directly: start/stop requests are set
// as flags that the control task picks up on its next iteration.

#include <Arduino.h>
#include "WiFi.h"
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <secrets.h>
#include "pid_controller.h"
#include "shot_history.h"
#include "dashboard.h"

extern Shot shot;           // from shot_stopper.h (included before this header)
extern float currentWeight; // live scale reading, owned by the control task

#define WIFI_CONNECT_TIMEOUT_MS 15000

AsyncWebServer server(80);
bool wifiConnected = false;

// Set by HTTP handlers, consumed by the control task
volatile bool webStartRequest = false;
volatile bool webStopRequest = false;
volatile bool webResetRequest = false;  // stop ESP/scale shot without pressing the machine button

// PID controller to monitor/tune, set by initializeServer()
PIDController* webPid = nullptr;

const char* endReasonName(int end) {
  switch (end) {
    case ENDTYPE::BUTTON: return "BUTTON";
    case ENDTYPE::WEIGHT: return "WEIGHT";
    case ENDTYPE::TIME:   return "TIME";
    case ENDTYPE::WEB:    return "WEB";
    default:              return "UNDEF";
  }
}

// Connect to WiFi with a timeout so a missing network can't hang boot forever.
// Returns true if connected; the web server is only started when it is.
bool initializeWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  DEBUG_STARTUP_PRINT("Connecting to WiFi '%s'...", ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    DEBUG_STARTUP_PRINT("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
  } else {
    DEBUG_STARTUP_PRINT("WiFi connection failed after %d ms - web dashboard disabled",
                        WIFI_CONNECT_TIMEOUT_MS);
  }
  return wifiConnected;
}

void initializeServer(Shot* s, PIDController* pid) {
  webPid = pid;

  // Dashboard page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", DASHBOARD_HTML);
  });

  // Single JSON state endpoint: everything the dashboard polls, in one request
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["brewing"] = shot.brewing;
    doc["shotTimer"] = shot.shotTimer;
    doc["expectedEnd"] = shot.expected_end_s;
    doc["weight"] = currentWeight;
    doc["goalWeight"] = shot.goalWeight;
    doc["weightOffset"] = shot.weightOffset;
    doc["pressure"] = shot.pressure;
    doc["goalPressure"] = shot.current_goal_pressure;
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
      times.add(shot.pressureGoalByTime[i].time_s);
      pressures.add(shot.pressureGoalByTime[i].pressure);
    }
    for (int i = 0; i < shot.numPressureGoalsByTimeLeft; i++) {
      times.add(-shot.pressureGoalByTimeLeft[i].time_left_s);
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
      float gw = req->getParam("value")->value().toFloat();
      if (gw >= 10 && gw <= 200) {
        shot.goalWeight = gw;
        EEPROM.write(WEIGHT_ADDR, (uint8_t)gw);
        EEPROM.commit();
      }
    }
    req->send(200, "text/plain", "OK");
  });

  server.on("/set_weight_offset", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("value")) {
      float off = req->getParam("value")->value().toFloat();
      if (off >= 0 && off <= MAX_OFFSET) {
        shot.weightOffset = off;
        EEPROM.write(OFFSET_ADDR, (uint8_t)(off * 10));
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
      int nByTime = 0, nByTimeLeft = 0;

      while (times.length() && pressures.length() &&
             (nByTime < MAX_PRESSURE_GOALS || nByTimeLeft < MAX_PRESSURE_GOALS)) {
        int tIdx = times.indexOf(',');
        String t = tIdx >= 0 ? times.substring(0, tIdx) : times;
        times = tIdx >= 0 ? times.substring(tIdx + 1) : "";
        int pIdx = pressures.indexOf(',');
        String p = pIdx >= 0 ? pressures.substring(0, pIdx) : pressures;
        pressures = pIdx >= 0 ? pressures.substring(pIdx + 1) : "";

        float timeVal = t.toFloat();
        float pressureVal = p.toFloat();

        if (timeVal < 0 && nByTimeLeft < MAX_PRESSURE_GOALS) {
          byTimeLeft[nByTimeLeft].time_left_s = -timeVal;
          byTimeLeft[nByTimeLeft].pressure = pressureVal;
          nByTimeLeft++;
        } else if (timeVal >= 0 && nByTime < MAX_PRESSURE_GOALS) {
          byTime[nByTime].time_s = timeVal;
          byTime[nByTime].pressure = pressureVal;
          nByTime++;
        }
      }

      shot.numPressureGoalsByTime = 0;
      shot.numPressureGoalsByTimeLeft = 0;
      memcpy(shot.pressureGoalByTime, byTime, sizeof(byTime));
      memcpy(shot.pressureGoalByTimeLeft, byTimeLeft, sizeof(byTimeLeft));
      shot.numPressureGoalsByTime = nByTime;
      shot.numPressureGoalsByTimeLeft = nByTimeLeft;
      DEBUG_SHOT_PRINT("Pressure profile set via web: %d by-time, %d by-time-left goals",
                       nByTime, nByTimeLeft);
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
        o["duration"] = rec.duration_s;
        o["finalWeight"] = rec.finalWeight;
        o["peakPressure"] = rec.peakPressure;
        o["endReason"] = endReasonName(rec.endReason);
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
            t.add(rec.time_s[j]);
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

  server.begin();
  DEBUG_STARTUP_PRINT("Web server started on port 80");
}

#endif // ESPRESSO_WEBSERVER_H
