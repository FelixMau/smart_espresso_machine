#include <Arduino.h>	
#include <EEPROM.h>
#include "shot_stopper.h"
#include "AcaiaArduinoBLE.h"
#include "webserver.h"


const int DIMMER_PIN = 5;
#define ZERO_CROSS_PIN 4  ///< GPIO pin connected to zero-cross detector output
#define PHASE_NUM 0        ///< Phase number (0 for single-phase systems)
#define INITIAL_BRIGHTNESS 100  ///< Initial brightness level in percent (0-100)
#define MAINS_FREQUENCY 0      ///< Mains frequency: 0=auto-detect, 50=50Hz, 60=60Hz

// Define global variables
float read_goalWeight = 36.0;
float read_weightOffset = 1.5;
float currentWeight = 0.0;
bool brewing = false;
float shotTimer = 0.0;
float expectedEnd = 0.0;

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);

  // Get stored setpoint and offset
  read_goalWeight = EEPROM.read(WEIGHT_ADDR);
  read_weightOffset = EEPROM.read(OFFSET_ADDR) / 10.0;
  Serial.print("Goal Weight retrieved: ");
  Serial.println(read_goalWeight);
  Serial.print("offset retrieved: ");
  Serial.println(read_weightOffset);

  // If EEPROM isn't initialized and has an unreasonable weight/offset, default to 36g/1.5g
  if ((read_goalWeight < 10) || (read_goalWeight > 200)) {
    read_goalWeight = 36;
    Serial.print("Goal Weight set to: ");
    Serial.println(read_goalWeight);
  }
  if (read_weightOffset > MAX_OFFSET) {
    read_weightOffset = 1.5;
    Serial.print("Offset set to: ");
    Serial.println(read_weightOffset);
  }

  // Initialize the GPIO hardware
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(in, INPUT_PULLUP);
  pinMode(OUT, OUTPUT);
  pinMode(DIMMER_PIN, OUTPUT); // Add for simple on/off control

  // Initialize the BLE hardware (only for internal use, no advertising)
  BLE.begin();
  BLE.setLocalName("shotStopper");
  Serial.println("Bluetooth® initialized for scale connection only.");

  // Initialize WiFi and web server
  //initializeWiFi();
  //initializeServer(&shot);

  // Remove all RBDimmer logic and frequency detection
  // Instead, just set the dimmer pin LOW (off) initially
  digitalWrite(DIMMER_PIN, HIGH);
  shot.goalWeight = read_goalWeight;
  shot.weightOffset = read_weightOffset;

  startwifi(); // Start WiFi connection
}



void loop() {
  // Connect to scale
  while (!scale.isConnected()) {
    scale.init();
    digitalWrite(DIMMER_PIN, HIGH); // Ensure pump is ON
    currentWeight = 0;
    if (shot.brewing) {
      setBrewingState(false);
    }
  }


  // Check for setpoint updates
    

  // Send a heartbeat message to the scale periodically to maintain connection
  if (scale.heartbeatRequired()) {
    scale.heartbeat();
  }

  // Always call newWeightAvailable to actually receive the datapoint from the scale,
  // otherwise getWeight() will return stale data
  if (scale.newWeightAvailable()) {
    currentWeight = scale.getWeight();

    Serial.print(currentWeight);
    //updateWebServer(&shot);

    // Update shot trajectory
    updateShotTrajectory(&shot, currentWeight); // Pass nullptr for dimmer
  }

  handleButtonLogic();
  
  handleMaxDurationReached(&shot);
  handleShotEnd(&shot, currentWeight);
  detectShotError(&shot, currentWeight);
}
