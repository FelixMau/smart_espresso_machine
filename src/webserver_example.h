#include <WiFi.h>       // standard library
#include <WebServer.h>  // standard library
#include "SuperMon.h"   // .h file that stores your html page code
#include <Arduino.h>  // standard library
#include <secrets.h> // this is where you put your wifi credentials

// Declare missing functions
void SendWebsite();
void SendJSON();

// Declare missing variables
extern float goalWeight;
extern float weightOffset;
extern float currentWeight;
extern bool brewing;
extern float shotTimer;
extern float expectedEnd;

// here you post web pages to your homes intranet which will make page debugging easier
// as you just need to refresh the browser as opposed to reconnection to the web server
#define USE_INTRANET

// once  you are read to go live these settings are what you client will connect to
#define AP_SSID "TestWebSite"
#define AP_PASS "023456789"

// start your defines for pins for sensors, outputs etc.
#define PIN_OUTPUT 26 // connected to nothing but an example of a digital write from the web page
#define PIN_FAN 27    // pin 27 and is a PWM signal to control a fan speed
#define PIN_LED 2     //On board LED
#define PIN_A0 34     // some analog input sensor


// variables to store measure data and sensor states
int BitsA0 = 0, BitsA1 = 0;
float VoltsA0 = 0, VoltsA1 = 0;
int FanSpeed = 0;
bool LED0 = false, SomeOutput = false;
uint32_t SensorUpdate = 0;
int FanRPM = 0;

// the XML array size needs to be bigger that your maximum expected size. 2048 is way too big for this example
char XML[2048];

// just some buffer holder for char operations
char buf[32];

// variable for the IP reported when you connect to your homes intranet (during debug mode)
IPAddress Actual_IP;

// definitions of your desired intranet created by the ESP32
IPAddress PageIP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress ip;

// gotta create a server
WebServer server(80);


// Utility functions
void printWifiStatus() {

  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  // print where to go in a browser:
  Serial.print("Open http://");
  Serial.println(ip);
}

// Initialization functions
void initializeWiFi() {
#ifdef USE_INTRANET
  WiFi.begin(LOCAL_SSID, LOCAL_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("IP address: "); Serial.println(WiFi.localIP());
  Actual_IP = WiFi.localIP();
#else
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  WiFi.softAPConfig(PageIP, gateway, subnet);
  delay(100);
  Actual_IP = WiFi.softAPIP();
  Serial.print("IP address: "); Serial.println(Actual_IP);
#endif
  printWifiStatus();
}

void initializeServer() {
  server.on("/", SendWebsite);
  server.on("/json", SendJSON);
  server.begin();
}

// Web server handlers
void SendWebsite() {

  Serial.println("sending web page");
  // you may have to play with this value, big pages need more porcessing time, and hence
  // a longer timeout that 200 ms
  server.send(200, "text/html", PAGE_MAIN);

}

void SendJSON() {
  // Create a JSON document
  StaticJsonDocument<512> jsonDoc;

  // Add data to the JSON document
  jsonDoc["goalWeight"] = goalWeight;
  jsonDoc["weightOffset"] = weightOffset;
  jsonDoc["currentWeight"] = currentWeight;
  jsonDoc["brewing"] = shot.brewing;
  jsonDoc["shotTimer"] = shot.time_s[0];
  jsonDoc["expectedEnd"] = shot.expected_end_s;

  // Optionally, add the latest value from the shot.time_s array
  if (shot.datapoints > 0) {
    jsonDoc["latestShotTime"] = shot.time_s[shot.datapoints - 1];
  }

  // Serialize the JSON document to a string
  String jsonString;
  serializeJson(jsonDoc, jsonString);

  // Send the JSON string to the client
  server.send(200, "application/json", jsonString);
}



// function managed by an .on method to handle slider actions on the web page
// this example will get the passed string called VALUE and conver to a pwm value
// and control the fan speed
void UpdateSlider() {

  // many I hate strings, but wifi lib uses them...
  String t_state = server.arg("VALUE");

  // conver the string sent from the web page to an int
  FanSpeed = t_state.toInt();
  Serial.print("UpdateSlider"); Serial.println(FanSpeed);
  // now set the PWM duty cycle
  ledcWrite(0, FanSpeed);


  // YOU MUST SEND SOMETHING BACK TO THE WEB PAGE--BASICALLY TO KEEP IT LIVE

  // option 1: send no information back, but at least keep the page live
  // just send nothing back
  // server.send(200, "text/plain", ""); //Send web page

  // option 2: send something back immediately, maybe a pass/fail indication, maybe a measured value
  // here is how you send data back immediately and NOT through the general XML page update code
  // my simple example guesses at fan speed--ideally measure it and send back real data
  // i avoid strings at all caost, hence all the code to start with "" in the buffer and build a
  // simple piece of data
  FanRPM = map(FanSpeed, 0, 255, 0, 2400);
  strcpy(buf, "");
  sprintf(buf, "%d", FanRPM);
  
  // now send it back
  server.send(200, "text/plain", buf); //Send web page

}

// now process button_0 press from the web site. Typical applications are the used on the web client can
// turn on / off a light, a fan, disable something etc
void ProcessButton_0() {

  //


  LED0 = !LED0;
  digitalWrite(PIN_LED, LED0);
  Serial.print("Button 0 "); Serial.println(LED0);
  // regardless if you want to send stuff back to client or not
  // you must have the send line--as it keeps the page running
  // if you don't want feedback from the MCU--or let the XML manage
  // sending feeback

  // option 1 -- keep page live but dont send any thing
  // here i don't need to send and immediate status, any status
  // like the illumination status will be send in the main XML page update
  // code
  server.send(200, "text/plain", ""); //Send web page

  // option 2 -- keep page live AND send a status
  // if you want to send feed back immediataly
  // note you must have reading code in the java script
  /*
    if (LED0) {
    server.send(200, "text/plain", "1"); //Send web page
    }
    else {
    server.send(200, "text/plain", "0"); //Send web page
    }
  */

}

// same notion for processing button_1
void ProcessButton_1() {

  // just a simple way to toggle a LED on/off. Much better ways to do this
  Serial.println("Button 1 press");
  SomeOutput = !SomeOutput;

  digitalWrite(PIN_OUTPUT, SomeOutput);
Serial.print("Button 1 "); Serial.println(LED0);
  // regardless if you want to send stuff back to client or not
  // you must have the send line--as it keeps the page running
  // if you don't want feedback from the MCU--or send all data via XML use this method
  // sending feeback
  server.send(200, "text/plain", ""); //Send web page

  // if you want to send feed back immediataly
  // note you must have proper code in the java script to read this data stream
  /*
    if (some_process) {
    server.send(200, "text/plain", "SUCCESS"); //Send web page
    }
    else {
    server.send(200, "text/plain", "FAIL"); //Send web page
    }
  */
}

// Main server loop
void handleClientRequests() {
  server.handleClient();
}

void updateSensorData() {
  // Only update if new weight data is available
  if (scale.newWeightAvailable()) {
    currentWeight = scale.getWeight();
    Serial.print("Current weight: ");
    Serial.println(currentWeight);

    // Update shot timer and check if goal weight is reached
    if (brewing) {
      shotTimer += 0.05; // Simulate shot timer increment
      if (currentWeight >= goalWeight - weightOffset) {
        Serial.println("Goal weight reached!");
      }
    }

    // Send updated data to the web server
    handleClientRequests();
    SendJSON();
  }
}


