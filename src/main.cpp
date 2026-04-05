#include <Arduino.h>
#include <EEPROM.h>
#include <ESP32Encoder.h>
#include "debug.h"
#include "shot_stopper.h"
#include "AcaiaArduinoBLE.h"
#include "webserver.h"

// ============================================================================
// TESTING CONFIGURATION
// ============================================================================
#define TESTING_MODE_NO_SCALE false    // Set to true to disable scale/BLE connection during testing
#define TESTING_PRINT_ENCODER true    // Print encoder position and PWM to Serial
#define TESTING_PRINT_SCALE_STATUS true // Print scale connection and pressure health status

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
int encoderAdjustment = 255;     // Encoder position (0-255 for PWM adjustment)

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Note: 'shot' is declared in shot_stopper.h and available globally
float currentWeight = 0.0;     // Current scale reading

// ============================================================================
// SETUP: Initialize all hardware and software components
// ============================================================================

  int finalPwmValue = 255;

void setup() {
  // CPU and serial configuration
  setCpuFrequencyMhz(80);
  Serial.begin(9600);
  delay(100);
  DEBUG_STARTUP_PRINT("========================================");
  DEBUG_STARTUP_PRINT("Smart Espresso Machine Starting");
  DEBUG_STARTUP_PRINT("========================================");

  // EEPROM initialization
  EEPROM.begin(EEPROM_SIZE);

  // Retrieve stored goal weight and offset from EEPROM
  shot.goalWeight = EEPROM.read(WEIGHT_ADDR);
  shot.weightOffset = EEPROM.read(OFFSET_ADDR) / 10.0;

  DEBUG_STARTUP_PRINT("Goal Weight retrieved: %.0f g", shot.goalWeight);
  DEBUG_STARTUP_PRINT("Offset retrieved: %.1f g", shot.weightOffset);

  // Validate EEPROM values; use defaults if unreasonable
  if ((shot.goalWeight < 10) || (shot.goalWeight > 200)) {
    shot.goalWeight = 36;
    DEBUG_STARTUP_PRINT("Goal Weight out of range, set to default: 36 g");
  }
  if (shot.weightOffset > MAX_OFFSET) {
    shot.weightOffset = 1.5;
    DEBUG_STARTUP_PRINT("Offset out of range, set to default: 1.5 g");
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
  DEBUG_STARTUP_PRINT("Display pins configured (ready for SPI display)");

  // Initialize encoder (half-quad mode for 2-pin configuration)
  //ESP32Encoder::useInternalWeakPullResistors = GPIO_PULLUP_ONLY;
  encoder.attachHalfQuad(encoderPinA, encoderPinB);
  encoder.setCount(0);
  DEBUG_STARTUP_PRINT("Encoder initialized (half-quad mode)");
 
  // Initialize PWM for dimmer control (Channel 0: Pump dimmer)
  ledcSetup(pwmChannel, freq, resolution);
  ledcAttachPin(DIMMER_PIN, pwmChannel);
  ledcWrite(pwmChannel, 255);  // Start with pump at FULL SPEED (idle state)
  DEBUG_STARTUP_PRINT("PWM dimmer initialized (50Hz, 8-bit) - Pump at 100%% (idle)");

  #if !TESTING_MODE_NO_SCALE
  // BLE initialization (for Acaia Lunar scale connection)
  BLE.begin();
  BLE.setLocalName("shotStopper");
  DEBUG_STARTUP_PRINT("Bluetooth initialized for scale connection");
  #else
  DEBUG_STARTUP_PRINT("TESTING MODE: Scale/BLE connection disabled");
  #endif

  // WiFi and web server initialization (optional)
  // Uncomment these lines if you have WiFi credentials configured in secrets.h
  // initializeWiFi();
  // initializeServer(&shot);

  DEBUG_STARTUP_PRINT("Setup Complete");
}


void printScalePressureStatus() {
  static unsigned long lastStatusMs = 0;
  if (!TESTING_PRINT_SCALE_STATUS) {
    return;
  }
  if (millis() - lastStatusMs < 1000) {
    return;
  }
  lastStatusMs = millis();
  int raw = analogRead(PRESSURE_PIN);
  float voltage = raw * (3.3f / 4095.0f);
  float pressure = 0.0f;
  if (voltage < 0.4f) {
    pressure = 0.0f;
  } else if (voltage > 3.3f) {
    pressure = 16.0f;
  } else {
    pressure = (voltage - 0.4f) * (16.0f / (3.3f - 0.4f));
  }



  DEBUG_SENSOR_PRINT("Pressure raw: %d | voltage: %.2f V | pressure: %.2f bar", raw, voltage, pressure);
}

// ============================================================================
// LOOP: Main execution loop
// ============================================================================

void loop() {
  // ========================================================================
  // SCALE CONNECTION AND DATA POLLING
  // ========================================================================

  #if !TESTING_MODE_NO_SCALE
  // Attempt to connect to scale if not connected
  // Falls back to manual pump control (dimmer) if scale unavailable
  while (!scale.isConnected()) {
    scale.init();
    ledcWrite(pwmChannel, 255);  // TESTING MODE: Keep pump at max while connecting
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

    DEBUG_SCALE_PRINT("Weight: %.1f g | Offset: %.1f g", currentWeight, shot.weightOffset);

    // Update shot trajectory with new weight datapoint
    updateShotTrajectory(&shot, currentWeight);
  }
  #else
  // TESTING MODE: Skip scale connection and keep current weight at 0
  currentWeight = 0;
  #endif

  // ========================================================================
  // PUMP DIMMER CONTROL (Auto during shot, 100% when idle)
  // ========================================================================

  
  
  long encoderPosition = encoder.getCount();
  encoderAdjustment = constrain(encoderPosition, 0, 255);
  if (!shot.brewing) {
    // IDLE STATE: Pump at full speed (100% = 255)
    finalPwmValue = 255;
  } else if (shot.datapoints == 0) {
    // No pressure data yet; start pump full power until first measurement.
    finalPwmValue = 255;
    DEBUG_ENCODER_PRINT("BREWING - waiting for pressure data, PWM: %d", finalPwmValue);
  } else {
    // BREWING STATE: control pump power based on pressure error.
    float goalPressure = shot.current_goal_pressure;
    float pressureError = goalPressure - shot.pressure;
    const float pressureKp = 35.0f; // proportional gain for pressure control
    int pressurePwm = (int)round(pressureError * pressureKp + 128.0f);
    finalPwmValue = constrain(pressurePwm, 0, 255);
    DEBUG_ENCODER_PRINT("BREWING - target %.1f bar, current %.1f bar, error %.2f, PWM: %d",
      goalPressure, shot.pressure, pressureError, finalPwmValue);
  }
  

  
  DEBUG_ENCODER_PRINT("Encoder count: %ld | PWM output: %d", encoderPosition, finalPwmValue);
  

  if (!shot.brewing) {
    DEBUG_ENCODER_PRINT("IDLE - Pump at 100%% (255) | Encoder: %ld", encoderPosition);
  }
  

  // Apply final PWM value to dimmer
  ledcWrite(pwmChannel, finalPwmValue);

  // ========================================================================
  // BUTTON AND SHOT STATE MANAGEMENT
  // ========================================================================

  // Handle button state machine (debounced, machine-agnostic)
  handleButtonLogic();

  printScalePressureStatus();

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
