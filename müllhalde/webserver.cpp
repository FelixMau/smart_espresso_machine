#include <WiFi.h>
#include <WebServer.h>
#include "SuperMon.h"
#include <Arduino.h>
#include <secrets.h>
#include "shotStopper.h"
#include "globals.h" // Ensure globals.h is included

// Definitions
#define USE_INTRANET
#define AP_SSID "TestWebSite"
#define AP_PASS "023456789"
#define PIN_OUTPUT 26
#define PIN_FAN 27
#define PIN_LED 2
#define PIN_A0 34

// Function prototypes
void UpdateSlider();
void ProcessButton_0();
void ProcessButton_1();
void SendWebsite();
void SendXML();
void SendJSON();
void initializeWiFi();
void initializeServer(WebServer& server);
void handleClientRequests(WebServer& server);

// Function implementations
void UpdateSlider() {
    FanSpeed = server.arg("VALUE").toInt();
    ledcWrite(0, FanSpeed);
    server.send(200, "text/plain", String(FanSpeed));
}

void ProcessButton_0() {
    LED0 = !LED0;
    digitalWrite(PIN_LED, LED0);
    server.send(200, "text/plain", "");
}

void ProcessButton_1() {
    SomeOutput = !SomeOutput;
    digitalWrite(PIN_OUTPUT, SomeOutput);
    server.send(200, "text/plain", "");
}

void SendWebsite() {
    server.send(200, "text/html", PAGE_MAIN);
}

void SendXML() {
    strcpy(XML, "<?xml version='1.0'?>\n<Data>\n");
    sprintf(buf, "<GoalWeight>%d</GoalWeight>\n", goalWeight);
    strcat(XML, buf);
    sprintf(buf, "<CurrentWeight>%.2f</CurrentWeight>\n", currentWeight);
    strcat(XML, buf);
    strcat(XML, "</Data>\n");
    server.send(200, "text/xml", XML);
}

void SendJSON() {
    snprintf(buf, sizeof(buf), "{\"currentWeight\":%.2f,\"goalWeight\":%d}", currentWeight, goalWeight);
    server.send(200, "application/json", buf);
}

void initializeWiFi() {
    WiFi.begin(LOCAL_SSID, LOCAL_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected. IP address: " + WiFi.localIP().toString());
}

void initializeServer(WebServer& server) {
    server.on("/", SendWebsite);
    server.on("/xml", SendXML);
    server.on("/json", SendJSON);
    server.on("/UPDATE_SLIDER", UpdateSlider);
    server.on("/BUTTON_0", ProcessButton_0);
    server.on("/BUTTON_1", ProcessButton_1);
    server.begin();
    Serial.println("Web server initialized.");
}

void handleClientRequests(WebServer& server) {
    server.handleClient();
}
