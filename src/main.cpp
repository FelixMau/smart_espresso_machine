#include <Arduino.h>
#include <EEPROM.h>
#include <ESP32Encoder.h>
#include "shot_stopper.h"
#include "AcaiaArduinoBLE.h"
#include "webserver.h"

// ============================================================================
// PIN CONFIGURATION
// ============================================================================

// Encoder pins
const int encoderPinA = 23;    // Encoder pin A (half-quad mode)
const int encoderPinB = 25;    // Encoder pin B (half-quad mode)

// Pump Dimmer pins (PWM)
// Note: DIMMER_PIN is declared as 'extern' in shot_stopper.h
const int DIMMER_PIN = 5;      // PWM output pin (must match shot_stopper.h declaration)
const int freq = 50;           // PWM frequency (Hz)
const int pwmChannel = 0;      // PWM channel (0 for dimmer, 1 for display backlight)
const int resolution = 8;      // 8-bit resolution (0 to 255)

// Button and Solenoid pins (defined in shot_stopper.h, declared here for clarity)
// GPIO 13: IN (Button input)
// GPIO 22: OUT (Solenoid output)

// Pressure Sensor pin (defined in shot_stopper.h)
// GPIO 33: PRESSURE_PIN

// Display SPI pins (for future display integration)
const int TFT_CLK = 18;        // SPI Clock
const int TFT_MOSI = 19;       // SPI Data Out to display
const int TFT_MISO = 21;       // SPI Data In from display
const int TFT_CS = 17;         // Chip Select
const int TFT_DC = 16;         // Data/Command
const int TFT_RST = 14;        // Reset
const int TFT_BL = 2;          // Backlight PWM (future)

// ============================================================================
// ENCODER AND DIMMER STATE
// ============================================================================

ESP32Encoder encoder;
int encoderAdjustment = 0;     // Encoder position (0-255 for PWM adjustment)

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

  // Display pins (for future display integration - configured as inputs to avoid conflicts)
  pinMode(TFT_CLK, OUTPUT);
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_MISO, INPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  Serial.println("Display pins configured (ready for SPI display)");

  // Initialize encoder (half-quad mode for 2-pin configuration)
  //ESP32Encoder::useInternalWeakPullResistors = GPIO_PULLUP_ONLY;
  encoder.attachHalfQuad(encoderPinA, encoderPinB);
  encoder.setCount(0);
  Serial.println("Encoder initialized (half-quad mode)");

  // Initialize PWM for dimmer control (Channel 0: Pump dimmer)
  ledcSetup(pwmChannel, freq, resolution);
  ledcAttachPin(DIMMER_PIN, pwmChannel);
  ledcWrite(pwmChannel, 255);  // Start with pump at FULL SPEED (idle state)
  Serial.println("PWM dimmer initialized (50Hz, 8-bit) - Pump at 100% (idle)");

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
  // PUMP DIMMER CONTROL (Auto during shot, 100% when idle)
  // ========================================================================

  // Read current encoder position for shot adjustment
  long encoderPosition = encoder.getCount();
  encoderAdjustment = constrain(encoderPosition, 0, 255);

  int finalPwmValue = 0;

  if (!shot.brewing) {
    // IDLE STATE: Pump at full speed (100% = 255)
    finalPwmValue = 255;

    // Reset encoder to neutral position when not brewing
    if (encoderPosition != 0) {
      encoder.setCount(0);
      encoderAdjustment = 0;
    }
  } else {
    // BREWING STATE: Auto-controlled but adjustable by encoder
    // During shot, the pump is controlled automatically by shot logic
    // but can be adjusted +/- by encoder for real-time fine-tuning

    // Start with encoder adjustment (0-255 range)
    // This allows operator to modulate pump during extraction
    finalPwmValue = encoderAdjustment;

    if (DEBUG) {
      Serial.print("BREWING - Encoder adjustment: ");
      Serial.print(encoderAdjustment);
      Serial.print("/255 | PWM: ");
      Serial.println(finalPwmValue);
    }
  }

  // Apply final PWM value to dimmer
  ledcWrite(pwmChannel, finalPwmValue);

  if (DEBUG && !shot.brewing) {
    Serial.print("IDLE - Pump at 100% (255) | Encoder: ");
    Serial.println(encoderPosition);
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
