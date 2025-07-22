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
<script>
  setInterval(()=>fetch('/weight').then(r=>r.text()).then(v=>document.getElementById('weight').textContent=v), 1000);
  setInterval(()=>fetch('/pressure').then(r=>r.text()).then(v=>document.getElementById('pressure').textContent=v), 1000);
  setInterval(()=>fetch('/shottime').then(r=>r.text()).then(v=>document.getElementById('shottime').textContent=v), 1000);
</script>
</body></html>
)rawliteral";

// Prozessor für Platzhalter im HTML
String processor(const String& var){
  if (var == "WEIGHT")   return readShotWeight();
  if (var == "PRESSURE") return readShotPressure();
  if (var == "SHOTTIME") return readShotTime();
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

  // Server starten
  server.begin();
}
