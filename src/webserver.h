/*********
  Dummy Webserver Test
*********/

// Arduino-Grundbibliothek
#include <Arduino.h>
// WLAN und Async Webserver
#include "WiFi.h"
#include "AsyncTCP.h"
#include "ESPAsyncWebServer.h"
#include <secrets.h>
#include <EEPROM.h>
#include "shot_stopper.h"

extern struct Shot shot; // Defined in shot_stopper.h

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
  <p>Weight Offset: <span id="weight_offset">%WEIGHT_OFFSET%</span> g</p>
  <div style="margin-top:20px;">
    <input type="number" step="0.1" id="new_goal" placeholder="New goal">
    <button onclick="setGoalWeight()">Set Goal</button>
  </div>
<script>
  setInterval(()=>fetch('/weight').then(r=>r.text()).then(v=>document.getElementById('weight').textContent=v), 1000);
  setInterval(()=>fetch('/pressure').then(r=>r.text()).then(v=>document.getElementById('pressure').textContent=v), 1000);
  setInterval(()=>fetch('/shottime').then(r=>r.text()).then(v=>document.getElementById('shottime').textContent=v), 1000);
    setInterval(()=>fetch('/expected_end').then(r=>r.text()).then(v=>document.getElementById('expected_end').textContent=v), 1000);
    setInterval(()=>fetch('/goal_weight').then(r=>r.text()).then(v=>document.getElementById('goal_weight').textContent=v), 1000);
    setInterval(()=>fetch('/weight_offset').then(r=>r.text()).then(v=>document.getElementById('weight_offset').textContent=v), 1000);

  function setGoalWeight(){
    const val = document.getElementById('new_goal').value;
    fetch('/set_goal_weight', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'value=' + encodeURIComponent(val)
    }).then(()=>{
      document.getElementById('goal_weight').textContent = val;
    });
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

server.on("/set_goal_weight", HTTP_POST, [](AsyncWebServerRequest *req){
  if(req->hasParam("value", true)){
    float val = req->getParam("value", true)->value().toFloat();
    shot.goalWeight = val;
    EEPROM.write(WEIGHT_ADDR, (uint8_t)val);
    EEPROM.commit();
    req->send(200, "text/plain", "ok");
  } else {
    req->send(400, "text/plain", "missing value");
  }
});

  // Server starten
  server.begin();
}
