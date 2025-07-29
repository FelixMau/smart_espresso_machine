/*********
  Dummy Webserver Test
*********/

// Arduino-Grundbibliothek
#include <Arduino.h>
// WLAN und Async Webserver
#include "WiFi.h"
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"
#include <EEPROM.h>
#include <secrets.h>

extern struct Shot shot; // Include the Shot structure from shot_stopper.h

// Server auf Port 80
AsyncWebServer server(80);

String readShotWeight() {
  float w = shot.datapoints > 0 ? shot.weight[shot.datapoints-1] : 0.0;
  return String(w, 1);
}
String readShotPressure() {
  return String(shot.pressure, 1);
}
String readShotTime() {
  return String(shot.shotTimer, 1);
}
 
String readShotexpectedEnd() {
  return String(shot.expected_end_s, 1);
}

String readShotWeightGoal() {
  return String(shot.goalWeight, 1);
}   

String readShotWeightOffset() {
  return String(shot.weightOffset, 1);
}


// HTML-Seite mit Platzhaltern %TEMPERATURE% / %HUMIDITY%
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Arial; text-align: center; }
    h2 { font-size: 2.5rem; }
    p { font-size: 2rem; }
    .units { font-size: 1rem; }
  </style>
</head>
<body>
  <h2>Shot Monitor</h2>
  <p>Weight: <span id="weight">%WEIGHT%</span> g</p>
  <p>Pressure: <span id="pressure">%PRESSURE%</span> bar</p>
  <p>Time: <span id="shottime">%SHOTTIME%</span> s</p>
  <p>Expected End: <span id="expected_end">%EXPECTED_END%</span> s</p>
  <p>Goal Weight: <span id="goal_weight">%GOAL_WEIGHT%</span> g</p>
  <input id="goal_input" type="number" step="0.1" value="%GOAL_WEIGHT%"/>
  <button onclick="updateGoalWeight()">Set Goal</button>
  <h3>Pressure Profile</h3>
  <p>Times (comma separated): <input id="profile_times" type="text" value="" /></p>
  <p>Pressures (comma separated): <input id="profile_pressures" type="text" value="" /></p>
  <button onclick="updatePressureProfile()">Set Profile</button>
  <p>Weight Offset: <span id="weight_offset">%WEIGHT_OFFSET%</span> g</p>
<script>
  setInterval(()=>fetch('/weight').then(r=>r.text()).then(v=>document.getElementById('weight').textContent=v), 1000);
  setInterval(()=>fetch('/pressure').then(r=>r.text()).then(v=>document.getElementById('pressure').textContent=v), 1000);
  setInterval(()=>fetch('/shottime').then(r=>r.text()).then(v=>document.getElementById('shottime').textContent=v), 1000);
    setInterval(()=>fetch('/expected_end').then(r=>r.text()).then(v=>document.getElementById('expected_end').textContent=v), 1000);
    setInterval(()=>fetch('/goal_weight').then(r=>r.text()).then(v=>document.getElementById('goal_weight').textContent=v), 1000);
    setInterval(()=>fetch('/weight_offset').then(r=>r.text()).then(v=>document.getElementById('weight_offset').textContent=v), 1000);

  function updateGoalWeight() {
    const val = document.getElementById('goal_input').value;
    fetch('/set_goal_weight?value=' + val, {method: 'GET'});
  }

  function updatePressureProfile() {
    const times = document.getElementById('profile_times').value;
    const pressures = document.getElementById('profile_pressures').value;
    const url = '/set_pressure_profile?times=' + encodeURIComponent(times) + '&pressures=' + encodeURIComponent(pressures);
    fetch(url, {method: 'GET'});
  }
</script>
</body></html>
)rawliteral";

// Prozessor für Platzhalter im HTML
String processor(const String& var){
  if (var == "WEIGHT")   return readShotWeight();
  if (var == "PRESSURE") return readShotPressure();
  if (var == "SHOTTIME") return readShotTime();
    if (var == "EXPECTED_END") return readShotexpectedEnd();
    if (var == "GOAL_WEIGHT") return readShotWeightGoal();
    if (var == "WEIGHT_OFFSET") return readShotWeightOffset();
  return String();
}


void startwifi(){
  // Serielle Ausgabe für Debug
 
  // WLAN verbinden
  WiFi.begin(ssid, password);
  Serial.print("Verbinde mit WLAN");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Verbunden, IP ist: ");
  Serial.println(WiFi.localIP());

  // root page with placeholders
server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send_P(200, "text/html", index_html, processor);
});

// JSON/text endpoints to call the getters
server.on("/weight", HTTP_GET, [](AsyncWebServerRequest *req){
  req->send(200, "text/plain", readShotWeight());
});
server.on("/pressure", HTTP_GET, [](AsyncWebServerRequest *req){
  req->send(200, "text/plain", readShotPressure());
});
server.on("/shottime", HTTP_GET, [](AsyncWebServerRequest *req){
  req->send(200, "text/plain", readShotTime());
});
server.on("/expected_end", HTTP_GET, [](AsyncWebServerRequest *req){
  req->send(200, "text/plain", readShotexpectedEnd());
});
server.on("/goal_weight", HTTP_GET, [](AsyncWebServerRequest *req){
  req->send(200, "text/plain", readShotWeightGoal());
});
server.on("/weight_offset", HTTP_GET, [](AsyncWebServerRequest *req){
  req->send(200, "text/plain", readShotWeightOffset());
});
server.on("/set_goal_weight", HTTP_GET, [](AsyncWebServerRequest *req){
  if (req->hasParam("value")) {
    float gw = req->getParam("value")->value().toFloat();
    shot.goalWeight = gw;
    EEPROM.write(WEIGHT_ADDR, (uint8_t)gw);
    EEPROM.commit();
  }
  req->send(200, "text/plain", "OK");
});
server.on("/set_pressure_profile", HTTP_GET, [](AsyncWebServerRequest *req){
  if (req->hasParam("times") && req->hasParam("pressures")) {
    String times = req->getParam("times")->value();
    String pressures = req->getParam("pressures")->value();
    shot.numPressureGoalsByTime = 0;
    shot.numPressureGoalsByTimeLeft = 0;
    while (times.length() && pressures.length() &&
           (shot.numPressureGoalsByTime < MAX_PRESSURE_GOALS ||
            shot.numPressureGoalsByTimeLeft < MAX_PRESSURE_GOALS)) {
      int tIdx = times.indexOf(',');
      String t = tIdx >= 0 ? times.substring(0, tIdx) : times;
      times = tIdx >= 0 ? times.substring(tIdx + 1) : "";
      int pIdx = pressures.indexOf(',');
      String p = pIdx >= 0 ? pressures.substring(0, pIdx) : pressures;
      pressures = pIdx >= 0 ? pressures.substring(pIdx + 1) : "";

      float timeVal = t.toFloat();
      float pressureVal = p.toFloat();

      if (timeVal < 0 && shot.numPressureGoalsByTimeLeft < MAX_PRESSURE_GOALS) {
        shot.pressureGoalByTimeLeft[shot.numPressureGoalsByTimeLeft].time_left_s = -timeVal;
        shot.pressureGoalByTimeLeft[shot.numPressureGoalsByTimeLeft].pressure = pressureVal;
        shot.numPressureGoalsByTimeLeft++;
      } else if (timeVal >= 0 && shot.numPressureGoalsByTime < MAX_PRESSURE_GOALS) {
        shot.pressureGoalByTime[shot.numPressureGoalsByTime].time_s = timeVal;
        shot.pressureGoalByTime[shot.numPressureGoalsByTime].pressure = pressureVal;
        shot.numPressureGoalsByTime++;
      }
    }
  }
  req->send(200, "text/plain", "OK");
});

  // Server starten
  server.begin();
}
