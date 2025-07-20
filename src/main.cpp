#include <Arduino.h>	
#include <EEPROM.h>
#include "shot_stopper_example.h"
#include "AcaiaArduinoBLE.h"
#include "webserver_example.h"
#include "secrets.h"
#include "rbdimmerESP32.h"

#define ZERO_CROSS_PIN 4  ///< GPIO pin connected to zero-cross detector output
#define DIMMER_PIN 5      ///< GPIO pin connected to dimmer control input
#define PHASE_NUM 0        ///< Phase number (0 for single-phase systems)
#define INITIAL_BRIGHTNESS 100  ///< Initial brightness level in percent (0-100)
#define MAINS_FREQUENCY 0      ///< Mains frequency: 0=auto-detect, 50=50Hz, 60=60Hz
rbdimmer_channel_t* dimmer = NULL;

// Define global variables
float goalWeight = 36.0;
float weightOffset = 1.5;
float currentWeight = 0.0;
bool brewing = false;
float shotTimer = 0.0;
float expectedEnd = 0.0;

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);

  // Get stored setpoint and offset
  goalWeight = EEPROM.read(WEIGHT_ADDR);
  weightOffset = EEPROM.read(OFFSET_ADDR) / 10.0;
  Serial.print("Goal Weight retrieved: ");
  Serial.println(goalWeight);
  Serial.print("offset retrieved: ");
  Serial.println(goalWeight);

  // If EEPROM isn't initialized and has an unreasonable weight/offset, default to 36g/1.5g
  if ((goalWeight < 10) || (goalWeight > 200)) {
    goalWeight = 36;
    Serial.print("Goal Weight set to: ");
    Serial.println(goalWeight);
  }
  if (weightOffset > MAX_OFFSET) {
    weightOffset = 1.5;
    Serial.print("Offset set to: ");
    Serial.println(weightOffset);
  }

  // Initialize the GPIO hardware
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(in, INPUT_PULLUP);
  pinMode(OUT, OUTPUT);

  // Initialize the BLE hardware (only for internal use, no advertising)
  BLE.begin();
  BLE.setLocalName("shotStopper");
  BLE.setAdvertisedService(weightService);
  weightService.addCharacteristic(weightCharacteristic);
  BLE.addService(weightService);
  weightCharacteristic.writeValue(goalWeight);
  Serial.println("Bluetooth® initialized for scale connection only.");

  // Initialize WiFi and web server
  initializeWiFi();
  initializeServer();

      // Print welcome message
    Serial.println("\n=== RBDimmer Basic Example ===");
    Serial.println("Initializing RBDimmer library...");
    
    // Step 1: Initialize the RBDimmer library
    rbdimmer_err_t err = rbdimmer_init();
    if (err != RBDIMMER_OK) {
        Serial.printf("ERROR: Failed to initialize library (error code: %d)\n", err);
        Serial.println("Check your ESP32 board and ensure enough memory is available");
        while (1) {
            delay(1000); // Halt execution on error
        }
    }
    Serial.println("Library initialized successfully");
    
    // Step 2: Register zero-cross detector
    Serial.printf("Registering zero-cross detector on pin %d...\n", ZERO_CROSS_PIN);
    err = rbdimmer_register_zero_cross(ZERO_CROSS_PIN, PHASE_NUM, MAINS_FREQUENCY);
    if (err != RBDIMMER_OK) {
        Serial.printf("ERROR: Failed to register zero-cross detector (error code: %d)\n", err);
        Serial.println("Check your wiring and ensure the pin supports interrupts");
        while (1) {
            delay(1000); // Halt execution on error
        }
    }
    Serial.println("Zero-cross detector registered");
    
    // Step 3: Configure and create dimmer channel
    Serial.println("Creating dimmer channel...");
    
    rbdimmer_config_t config = {
        .gpio_pin = DIMMER_PIN,
        .phase = PHASE_NUM,
        .initial_level = 0,  // Start with light off
        .curve_type = RBDIMMER_CURVE_RMS  // RMS curve for incandescent bulbs
    };
    
    err = rbdimmer_create_channel(&config, &dimmer);
    if (err != RBDIMMER_OK) {
        Serial.printf("ERROR: Failed to create dimmer channel (error code: %d)\n", err);
        Serial.println("Check your pin configuration and available timers");
        while (1) {
            delay(1000); // Halt execution on error
        }
    }
    Serial.println("Dimmer channel created successfully");
    
    // Step 4: Set brightness level
    Serial.printf("Setting brightness to %d%%...\n", INITIAL_BRIGHTNESS);
    err = rbdimmer_set_level(dimmer, INITIAL_BRIGHTNESS);
    if (err != RBDIMMER_OK) {
        Serial.printf("WARNING: Failed to set brightness (error code: %d)\n", err);
    } else {
        Serial.printf("Dimmer is now running at %d%% brightness\n", INITIAL_BRIGHTNESS);
    }
    
    // Print status information
    Serial.println("\nSetup complete! Dimmer is active.");
    Serial.println("The connected load should now be at 50% brightness");
    
    // Wait a moment for frequency detection
    delay(500);
    
    // Print detected frequency
    uint16_t frequency = rbdimmer_get_frequency(PHASE_NUM);
    if (frequency > 0) {
        Serial.printf("Detected mains frequency: %d Hz\n", frequency);
    } else {
        Serial.println("Frequency detection in progress...");
    }
}



void loop() {
  // Connect to scale
  while (!scale.isConnected()) {
    scale.init();
    rbdimmer_set_level(dimmer, 100); // Ensure pump is on
    currentWeight = 0;
    if (shot.brewing) {
      setBrewingState(false);
    }
  }


  // Check for setpoint updates
  BLE.poll();
  if (weightCharacteristic.written()) {
    Serial.print("goal weight updated from ");
    Serial.print(goalWeight);
    Serial.print(" to ");
    goalWeight = weightCharacteristic.value();
    Serial.println(goalWeight);
    EEPROM.write(WEIGHT_ADDR, goalWeight); // 1 byte, 0-255
    EEPROM.commit();
  }

  // Send a heartbeat message to the scale periodically to maintain connection
  if (scale.heartbeatRequired()) {
    scale.heartbeat();
  }

  // Always call newWeightAvailable to actually receive the datapoint from the scale,
  // otherwise getWeight() will return stale data
  if (scale.newWeightAvailable()) {
    currentWeight = scale.getWeight();

    Serial.print(currentWeight);
    updateSensorData(&shot);

    // Update shot trajectory
    updateShotTrajectory(&shot, currentWeight, goalWeight, weightOffset, dimmer);
  }

  handleButtonLogic();
  
  handleMaxDurationReached(&shot);
  handleShotEnd(&shot, currentWeight);
  detectShotError(&shot, currentWeight, goalWeight, &weightOffset);
}
