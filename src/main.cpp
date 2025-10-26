#include <Arduino.h>
#include "rbdimmerESP32.h"

#define ZERO_CROSS_PIN 4  ///< GPIO pin connected to zero-cross detector output
#define DIMMER_PIN 5      ///< GPIO pin connected to dimmer control input
#define PHASE_NUM 0        ///< Phase number (0 for single-phase systems)
#define INITIAL_BRIGHTNESS 95  ///< Initial brightness level in percent (0-100)
#define MAINS_FREQUENCY 0      ///< Mains frequency: 0=auto-detect, 50=50Hz, 60=60Hz
rbdimmer_channel_t* dimmer = NULL;

void setup() {
    // Initialize serial communication for debug output
    Serial.begin(9600);
    
    // Wait for serial port to connect (needed for native USB boards)
    while (!Serial && millis() < 3000) {
        ; // Wait up to 3 seconds for serial connection
    }
    
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
    // Static variable to track last print time
    static unsigned long lastPrintTime = 0;
    
    // Print status every 5 seconds
    if (millis() - lastPrintTime >= 5000) {
        lastPrintTime = millis();
        
        // Get current dimmer status
        uint8_t currentLevel = rbdimmer_get_level(dimmer);
        uint16_t frequency = rbdimmer_get_frequency(PHASE_NUM);
        bool isActive = rbdimmer_is_active(dimmer);
        
        // Print status information
        Serial.println("\n--- Dimmer Status ---");
        Serial.printf("Brightness: %d%%\n", currentLevel);
        Serial.printf("Frequency: %d Hz\n", frequency);
        Serial.printf("Active: %s\n", isActive ? "Yes" : "No");
        Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
        Serial.println("-------------------");
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}