#include <Arduino.h>
#include <EEPROM.h>
#include <ESP32Encoder.h>
#include <AcaiaArduinoBLE.h>
#include <WiFi.h>

#include "debug.h"
#include "pid_controller.h"
#include "shot_history.h"
#include "shot_stopper.h"
#include "webserver.h"

// ============================================================================
// TESTING CONFIGURATION
// ============================================================================

#define TESTING_MODE_NO_SCALE false     // Set to true to disable scale/BLE connection during testing
#define TESTING_PRINT_SCALE_STATUS true // Print scale connection and pressure health status

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// Button input, button output (PRESS_BUTTON_PIN, GPIO 22) and the pressure
// sensor (PRESSURE_PIN, GPIO 32) are defined in shot_stopper.h.

// Encoder pins (half-quad mode)
const int ENCODER_PIN_A = 23;
const int ENCODER_PIN_B = 25;

// Pump dimmer PWM
const int DIMMER_PIN = 5;           // PWM output pin
const int PWM_FREQUENCY_HZ = 50;    // PWM frequency
const int PWM_CHANNEL = 0;          // LEDC channel (0 for dimmer, 1 for display backlight)
const int PWM_RESOLUTION_BITS = 8;  // 8-bit resolution (0 to 255)

// Display SPI pins (for future display integration)
const int TFT_CLK = 18;   // SPI clock
const int TFT_MOSI = 19;  // SPI data out to display
const int TFT_MISO = 21;  // SPI data in from display
const int TFT_CS = 17;    // Chip select
const int TFT_DC = 16;    // Data/command
const int TFT_RST = 14;   // Reset
const int TFT_BL = 2;     // Backlight PWM (future)

// ============================================================================
// GLOBAL STATE
// ============================================================================
// 'shot' and 'currentWeight' are defined in shot_stopper.cpp: currentWeight
// is written by the scale task, read by the control task and web server.

ESP32Encoder encoder;

// PID controller for pressure regulation, live-tunable via the web dashboard.
// Gentler defaults than the original 25/0.5/8: with the measurement now
// sampled at 20 Hz and the derivative filtered, less gain is needed and the
// old values limit-cycled (+/-2-3 bar swings every few seconds).
PIDController pressurePID(15.0f, 1.0f, 5.0f);

// Real-time control loop task (defined below), started from setup()
void controlTask(void* param);
// Background scale connection/polling task (defined below), started from setup()
void scaleTask(void* param);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  // CPU and serial configuration
  // 240 MHz: WiFi (web server) + BLE (scale) share the radio and need headroom
  setCpuFrequencyMhz(240);
  Serial.begin(9600);
  delay(100);
  DEBUG_STARTUP_PRINT("========================================");
  DEBUG_STARTUP_PRINT("Smart Espresso Machine Starting");
  DEBUG_STARTUP_PRINT("========================================");

  // Retrieve stored goal weight and offset from EEPROM
  EEPROM.begin(EEPROM_SIZE);
  shot.goalWeight = EEPROM.read(WEIGHT_ADDR);
  shot.weightOffset = EEPROM.read(OFFSET_ADDR) / 10.0f;

  DEBUG_STARTUP_PRINT("Goal Weight retrieved: %.0f g", shot.goalWeight);
  DEBUG_STARTUP_PRINT("Offset retrieved: %.1f g", shot.weightOffset);

  // Validate EEPROM values; use defaults if unreasonable
  if ((shot.goalWeight < 10) || (shot.goalWeight > 200)) {
    shot.goalWeight = 36;
    DEBUG_STARTUP_PRINT("Goal Weight out of range, set to default: 36 g");
  }
  if (shot.weightOffset > MAX_OFFSET) {
    shot.weightOffset = 1.5f;
    DEBUG_STARTUP_PRINT("Offset out of range, set to default: 1.5 g");
  }

  // GPIO pin initialization
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_INPUT_PIN, INPUT_PULLUP);  // Button input
  pinMode(PRESS_BUTTON_PIN, OUTPUT);        // Button output (opto-isolated)
  pinMode(DIMMER_PIN, OUTPUT);              // Dimmer PWM output

  // Display pins (for future display integration)
  pinMode(TFT_CLK, OUTPUT);
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_MISO, INPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  DEBUG_STARTUP_PRINT("Display pins configured (ready for SPI display)");

  // Initialize encoder (half-quad mode for 2-pin configuration)
  encoder.attachHalfQuad(ENCODER_PIN_A, ENCODER_PIN_B);
  encoder.setCount(0);
  DEBUG_STARTUP_PRINT("Encoder initialized (half-quad mode)");

  // Initialize PWM for dimmer control (channel 0: pump dimmer)
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(DIMMER_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 255);  // Start with pump at FULL SPEED (idle state)
  DEBUG_STARTUP_PRINT("PWM dimmer initialized (50Hz, 8-bit) - Pump at 100%% (idle)");

  #if !TESTING_MODE_NO_SCALE
  // BLE initialization (for Acaia Lunar scale connection)
  BLE.begin();
  BLE.setLocalName("shotStopper");
  DEBUG_STARTUP_PRINT("Bluetooth initialized for scale connection");
  #else
  DEBUG_STARTUP_PRINT("TESTING MODE: Scale/BLE connection disabled");
  scaleConnected = true;  // Pretend connected so the dashboard start button works
  #endif

  // Shot history buffer (RAM-only, read by the web server)
  initShotHistory();

  // WiFi and web server (LAN dashboard). Boot continues without the
  // dashboard if WiFi is unavailable (15s timeout).
  if (initializeWiFi()) {
    initializeServer(&pressurePID);
  }

  // Run the real-time control loop as its own FreeRTOS task on core 1 with
  // priority above loopTask, so the async web server (core 0) and anything
  // in loop() can never stall scale polling, trajectory updates, or pump PWM.
  xTaskCreatePinnedToCore(controlTask, "control", 10240, nullptr, 2, nullptr, 1);

  #if !TESTING_MODE_NO_SCALE
  // Scale connection runs as a background task at priority 1 (below the
  // control task) so a missing scale can never stall brewing logic or the
  // web dashboard. Core 1: the task-watchdogged core-0 idle task must not
  // be starved by the blocking 10 s BLE scan inside scale.init().
  xTaskCreatePinnedToCore(scaleTask, "scale", 8192, nullptr, 1, nullptr, 1);
  #endif

  DEBUG_STARTUP_PRINT("Setup Complete");
}

// ============================================================================
// STATUS LOGGING
// ============================================================================

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
  float pressure = pressureBarFromVoltage(voltage);
  DEBUG_SENSOR_PRINT("Pressure raw: %d | voltage: %.2f V | pressure: %.2f bar",
                     raw, voltage, pressure);
}

// Re-print the dashboard URL every 10s so it can't scroll away in the log
void printWifiStatus() {
  static unsigned long lastWifiStatusMs = 0;
  if (millis() - lastWifiStatusMs < 10000) {
    return;
  }
  lastWifiStatusMs = millis();
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_STARTUP_PRINT("Dashboard: http://%s/", WiFi.localIP().toString().c_str());
  } else {
    DEBUG_STARTUP_PRINT("WiFi not connected - dashboard unavailable");
  }
}

// ============================================================================
// CONTROL LOOP: Real-time control iteration
// ============================================================================
// Runs in its own FreeRTOS task (core 1, priority 2) so the async web server
// on core 0 can never stall scale polling, trajectory updates, or pump PWM.

void controlIteration() {
  // ========================================================================
  // PRESSURE SAMPLING (every iteration, ~20 Hz)
  // ========================================================================
  // The PID below must never act on a stale reading: sampling only at the
  // scale's ~2 Hz weight rate made the measurement jump between control
  // iterations, and the derivative term slammed the pump rail-to-rail on
  // every jump (the root cause of "jumpy" pressure).

  updatePressureSensor(&shot);

  // ========================================================================
  // SCALE CONNECTION AND DATA POLLING
  // ========================================================================

  #if !TESTING_MODE_NO_SCALE
  // Scale connection/polling is handled by scaleTask in the background;
  // this task never blocks on BLE and just consumes the shared state.
  if (!scaleConnected) {
    // Discard stale dashboard start clicks so a shot can't fire the moment
    // the scale reconnects
    webStartRequest = false;
    if (shot.brewing) {
      DEBUG_SHOT_PRINT("Scale disconnected during shot - ending shot (machine button untouched)");
      shot.brewing = false;
      setBrewingState(false);
    }
  } else if (scaleNewWeight) {
    scaleNewWeight = false;

    DEBUG_SCALE_PRINT("Weight: %.1f g | Offset: %.1f g", currentWeight, shot.weightOffset);

    // Update shot trajectory with new weight datapoint
    updateShotTrajectory(&shot, currentWeight);
  }
  #else
  // TESTING MODE: Skip scale connection and keep current weight at 0
  currentWeight = 0;
  #endif

  // ========================================================================
  // WEB DASHBOARD START/STOP REQUESTS
  // ========================================================================
  // HTTP handlers only set flags; the scale/BLE is touched exclusively here
  // in the control task.

  if (webStartRequest) {
    webStartRequest = false;
    if (!shot.brewing) {
      DEBUG_SHOT_PRINT("Shot start requested via web - pressing machine button");
      if (MOMENTARY) {
        // Pulse the opto-coupled button to start the machine
        digitalWrite(PRESS_BUTTON_PIN, HIGH);
        delay(1000);
        digitalWrite(PRESS_BUTTON_PIN, LOW);
      } else {
        // Latching machine: hold the brew line via the output pin
        digitalWrite(PRESS_BUTTON_PIN, HIGH);
        buttonLatched = true;
      }
      shot.brewing = true;
      setBrewingState(true);
    }
  }
  if (webStopRequest) {
    webStopRequest = false;
    if (shot.brewing) {
      DEBUG_SHOT_PRINT("Shot stop requested via web - pressing machine button");
      shot.brewing = false;
      shot.end = EndType::WEB;  // Pulses the machine button like WEIGHT/TIME
      setBrewingState(false);
    }
  }
  if (webResetRequest) {
    webResetRequest = false;
    if (shot.brewing) {
      DEBUG_SHOT_PRINT("Shot reset requested via web - machine button untouched");
      shot.brewing = false;
      shot.end = EndType::BUTTON;  // BUTTON end skips the machine pulse
      setBrewingState(false);
    }
  }

  // ========================================================================
  // PUMP DIMMER CONTROL (PID during shot, 100% when idle)
  // ========================================================================

  long encoderPosition = encoder.getCount();

  int pwmValue = 255;
  if (!shot.brewing) {
    // IDLE STATE: Pump at full speed (100% = 255)
    pwmValue = 255;
  } else if (shot.datapoints == 0) {
    // Shot just started, no pressure data yet - reset PID and run full power
    pressurePID.reset();
    pwmValue = 255;
    DEBUG_ENCODER_PRINT("BREWING - waiting for pressure data, PWM: %d", pwmValue);
  } else {
    // BREWING STATE: PID control for pressure regulation
    float goalPressure = shot.currentGoalPressure;
    pwmValue = pressurePID.calculate(goalPressure, shot.pressure);
    DEBUG_ENCODER_PRINT("BREWING - target %.1f bar, current %.1f bar, PWM: %d",
                        goalPressure, shot.pressure, pwmValue);
  }

  DEBUG_ENCODER_PRINT("Encoder count: %ld | PWM output: %d", encoderPosition, pwmValue);
  if (!shot.brewing) {
    DEBUG_ENCODER_PRINT("IDLE - Pump at 100%% (255) | Encoder: %ld", encoderPosition);
  }

  // Apply final PWM value to dimmer and publish it for the web dashboard
  ledcWrite(PWM_CHANNEL, pwmValue);
  shot.pumpPwm = pwmValue;

  // ========================================================================
  // BUTTON AND SHOT STATE MANAGEMENT
  // ========================================================================

  // Handle button state machine (debounced, machine-agnostic)
  handleButtonLogic();

  printScalePressureStatus();
  printWifiStatus();

  // Monitor for maximum shot duration (safety timeout)
  handleMaxDurationReached(&shot);

  // Detect shot end conditions (weight target, time limit, button release)
  handleShotEnd(&shot, currentWeight);

  // Post-shot error detection and EEPROM learning
  // Learns weight offset if final weight is within 5g of goal
  detectShotError(&shot, currentWeight);
}

// Control task: run the control iteration at a fixed ~50 ms cadence.
// The web server runs asynchronously in the AsyncTCP task and never blocks this.
void controlTask(void* param) {
  for (;;) {
    controlIteration();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ============================================================================
// SCALE TASK: background BLE connection management and weight polling
// ============================================================================
// Owns ALL scale/BLE calls; everyone else talks to it via the flags in
// shot_stopper.h. scale.init() blocks up to 10 s while scanning - fine here,
// the control task and web server keep running. Between failed attempts the
// scan is stopped and the task backs off, because BLE scanning and WiFi share
// the one 2.4 GHz radio: scanning back-to-back starves WiFi until it drops.

#define SCALE_RETRY_BACKOFF_MS 5000

void scaleTask(void* param) {
  for (;;) {
    if (!scale.isConnected()) {
      scaleConnected = false;
      currentWeight = 0;
      // Drop commands queued while disconnected: they belong to a dead shot
      scaleStartSequenceRequest = false;
      scaleStopTimerRequest = false;
      scaleTareRequest = false;

      if (!scale.init()) {
        BLE.stopScan();  // init() leaves the scan running on timeout
        vTaskDelay(pdMS_TO_TICKS(SCALE_RETRY_BACKOFF_MS));
        continue;
      }
      DEBUG_SCALE_PRINT("Scale connected");
    }
    scaleConnected = true;

    // Heartbeat every ~30 s keeps the BLE connection alive
    if (scale.heartbeatRequired()) {
      scale.heartbeat();
    }

    // Poll continuously; without this the connection goes stale
    if (scale.newWeightAvailable()) {
      currentWeight = scale.getWeight();
      scaleNewWeight = true;
    }

    // Execute commands queued by the control task
    if (scaleStartSequenceRequest) {
      scaleStartSequenceRequest = false;
      scale.resetTimer();
      vTaskDelay(pdMS_TO_TICKS(50));  // Let the scale process each command
      scale.startTimer();
      if (AUTOTARE) {
        vTaskDelay(pdMS_TO_TICKS(50));
        scale.tare();
      }
    }
    if (scaleStopTimerRequest) {
      scaleStopTimerRequest = false;
      scale.stopTimer();
    }
    if (scaleTareRequest) {
      scaleTareRequest = false;
      scale.tare();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================================================
// LOOP: WiFi watchdog - all real-time work happens in controlTask
// ============================================================================
// BLE scanning can knock WiFi off the shared radio; without this the
// dashboard never comes back once WiFi drops.

void loop() {
  static unsigned long lastWifiRetryMs = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiRetryMs > 15000) {
      lastWifiRetryMs = millis();
      DEBUG_STARTUP_PRINT("WiFi down - attempting reconnect");
      WiFi.reconnect();
    }
  } else if (!serverStarted) {
    // WiFi came up after boot timed out - start the dashboard now
    initializeServer(&pressurePID);
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
}
