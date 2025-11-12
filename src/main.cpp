#include <Arduino.h>
#include <EEPROM.h>
#include <ESP32Encoder.h>
#include "shot_stopper.h"
#include "AcaiaArduinoBLE.h"
#include "webserver.h"

// ============================================================================
// ENCODER AND PWM CONFIGURATION
// ============================================================================

ESP32Encoder encoder;

// Encoder configuration
const int encoderPinA = 23;    // Encoder pin A (half-quad mode)
const int encoderPinB = 25;    // Encoder pin B (half-quad mode)

// PWM/Dimmer configuration
// Note: DIMMER_PIN is declared as 'extern' in shot_stopper.h
const int DIMMER_PIN = 5;      // PWM output pin (must match shot_stopper.h declaration)
const int freq = 50;           // PWM frequency (Hz)
const int pwmChannel = 0;      // PWM channel
const int resolution = 8;      // 8-bit resolution (0 to 255)

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Note: 'shot' is declared in shot_stopper.h and available globally
float currentWeight = 0.0;     // Current scale reading

// ============================================================================
// SETUP: Initialize all hardware and software components
// ============================================================================

void setup() {
  // CPU and serial configuration
  setCpuFrequencyMhz(80);
  Serial.begin(9600);
  delay(100);
  Serial.println("\n\n=== Smart Espresso Machine Starting ===\n");

  // EEPROM initialization
  EEPROM.begin(EEPROM_SIZE);

  // Retrieve stored goal weight and offset from EEPROM
  shot.goalWeight = EEPROM.read(WEIGHT_ADDR);
  shot.weightOffset = EEPROM.read(OFFSET_ADDR) / 10.0;

  Serial.print("Goal Weight retrieved: ");
  Serial.println(shot.goalWeight);
  Serial.print("Offset retrieved: ");
  Serial.println(shot.weightOffset);

  // Validate EEPROM values; use defaults if unreasonable
  if ((shot.goalWeight < 10) || (shot.goalWeight > 200)) {
    shot.goalWeight = 36;
    Serial.println("Goal Weight out of range, set to default: 36g");
  }
  if (shot.weightOffset > MAX_OFFSET) {
    shot.weightOffset = 1.5;
    Serial.println("Offset out of range, set to default: 1.5g");
  }

  // GPIO pin initialization
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(in, INPUT_PULLUP);      // Button input
  pinMode(OUT, OUTPUT);            // Button output (opto-isolated)
  pinMode(DIMMER_PIN, OUTPUT);     // Dimmer PWM output

  // Initialize encoder (half-quad mode for 2-pin configuration)
  //ESP32Encoder::useInternalWeakPullResistors = GPIO_PULLUP_ONLY;
  encoder.attachHalfQuad(encoderPinA, encoderPinB);
  encoder.setCount(0);
  Serial.println("Encoder initialized (half-quad mode)");

  // Initialize PWM for dimmer control
  ledcSetup(pwmChannel, freq, resolution);
  ledcAttachPin(DIMMER_PIN, pwmChannel);
  ledcWrite(pwmChannel, 0);  // Start with pump off
  Serial.println("PWM dimmer initialized (50Hz, 8-bit)");

  // BLE initialization (for Acaia Lunar scale connection)
  BLE.begin();
  BLE.setLocalName("shotStopper");
  Serial.println("Bluetooth initialized for scale connection");

  // WiFi and web server initialization (optional)
  // Uncomment these lines if you have WiFi credentials configured in secrets.h
  // initializeWiFi();
  // initializeServer(&shot);

  Serial.println("=== Setup Complete ===\n");
}

// ============================================================================
// LOOP: Main execution loop
// ============================================================================

void loop() {
  // ========================================================================
  // SCALE CONNECTION AND DATA POLLING
  // ========================================================================

  // Attempt to connect to scale if not connected
  // Falls back to manual pump control (dimmer) if scale unavailable
  while (!scale.isConnected()) {
    scale.init();
    ledcWrite(pwmChannel, 0);  // Ensure pump is OFF while connecting
    currentWeight = 0;
    if (shot.brewing) {
      setBrewingState(false);
    }
    delay(100);
  }

  // Send heartbeat to scale periodically (~30s interval) to maintain BLE connection
  if (scale.heartbeatRequired()) {
    scale.heartbeat();
  }

  // Poll for new weight data from scale
  // CRITICAL: Must call this continuously or scale connection goes stale
  if (scale.newWeightAvailable()) {
    currentWeight = scale.getWeight();

    if (DEBUG) {
      Serial.print("Weight: ");
      Serial.print(currentWeight, 1);
      Serial.print("g | Offset: ");
      Serial.println(shot.weightOffset, 1);
    }

    // Update shot trajectory with new weight datapoint
    updateShotTrajectory(&shot, currentWeight);
  }

  // ========================================================================
  // ENCODER-TO-PWM MANUAL CONTROL
  // ========================================================================

  long encoderPosition = encoder.getCount();
  int pwmValue = constrain(encoderPosition, 0, 255);

  // Apply encoder value to dimmer PWM
  ledcWrite(pwmChannel, pwmValue);

  if (DEBUG) {
    Serial.print("Encoder: ");
    Serial.print(encoderPosition);
    Serial.print(" | PWM: ");
    Serial.println(pwmValue);
  }

  // ========================================================================
  // BUTTON AND SHOT STATE MANAGEMENT
  // ========================================================================

  // Handle button state machine (debounced, machine-agnostic)
  handleButtonLogic();

  // Monitor for maximum shot duration (safety timeout)
  handleMaxDurationReached(&shot);

  // Detect shot end conditions (weight target, time limit, button release)
  handleShotEnd(&shot, currentWeight);

  // Post-shot error detection and EEPROM learning
  // Learns weight offset if final weight is within 5g of goal
  detectShotError(&shot, currentWeight);

  // ========================================================================
  // OPTIONAL: WEB SERVER AND MONITORING
  // ========================================================================

  // Uncomment to enable web dashboard and API
  // updateWebServer(&shot);

  // ========================================================================
  // LOOP TIMING
  // ========================================================================

  // Small delay for stability and serial readability
  delay(50);
}
