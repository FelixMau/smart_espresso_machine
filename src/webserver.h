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


// Server auf Port 80
AsyncWebServer server(80);

// Dummy-Funktionen, statt DHT-Leseroutine
String readDummyTemperature() {
  // hier könnte auch random(20,30) stehen, wenn du unterschiedliche Werte testen willst
  float t = 24.3;
  Serial.printf("readDummyTemperature(): %.1f°C\n", t);
  return String(t, 1);
}

String readDummyHumidity() {
  float h = 55.8;
  Serial.printf("readDummyHumidity(): %.1f%%\n", h);
  return String(h, 1);
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
  <h2>ESP32 Dummy Server</h2>
  <p>
    Temperatur: <span id="temperature">%TEMPERATURE%</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p>
    Luftfeuchte: <span id="humidity">%HUMIDITY%</span>
    <sup class="units">&percnt;</sup>
  </p>
<script>
  // Temperatur alle 5s nachladen
  setInterval(() => {
    fetch('/temperature')
      .then(res => res.text())
      .then(val => document.getElementById('temperature').textContent = val);
  }, 5000);

  // Luftfeuchte alle 7s nachladen
  setInterval(() => {
    fetch('/humidity')
      .then(res => res.text())
      .then(val => document.getElementById('humidity').textContent = val);
  }, 7000);
</script>
</body>
</html>
)rawliteral";

// Prozessor für Platzhalter im HTML
String processor(const String& var){
  if(var == "TEMPERATURE"){
    return readDummyTemperature();
  }
  if(var == "HUMIDITY"){
    return readDummyHumidity();
  }
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

  // Root-Seite mit Platzhaltern ausliefern
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // JSON-Endpunkte für Dummy werte
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    String t = readDummyTemperature();
    request->send(200, "text/plain", t);
  });

  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    String h = readDummyHumidity();
    request->send(200, "text/plain", h);
  });

  // Server starten
  server.begin();
}
