#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================

// Master debug flag - set to false to disable all debug output and reduce memory
#define DEBUG_ENABLED true

// Category-specific debug flags (only active if DEBUG_ENABLED is true)
#define DEBUG_SHOT true
#define DEBUG_SENSOR true
#define DEBUG_BUTTON true
#define DEBUG_ENCODER true
#define DEBUG_SCALE true
#define DEBUG_PUMP true
#define DEBUG_STARTUP true
#define DEBUG_STATE false  // Verbose state machine logging

// ============================================================================
// SERIAL MACROS - Consistent formatting with timestamps and log levels
// ============================================================================

// Helper macro to print timestamp in format [HHH:MM:SS.mmm]
#define PRINT_TIME_HEADER(category) do { \
  unsigned long ms = millis(); \
  unsigned int s = (ms / 1000) % 60; \
  unsigned int m = (ms / 60000) % 60; \
  unsigned int h = (ms / 3600000); \
  Serial.print("["); \
  if (h < 10) Serial.print("0"); \
  Serial.print(h); \
  Serial.print(":"); \
  if (m < 10) Serial.print("0"); \
  Serial.print(m); \
  Serial.print(":"); \
  if (s < 10) Serial.print("0"); \
  Serial.print(s); \
  Serial.print("."); \
  unsigned int ms_part = ms % 1000; \
  if (ms_part < 100) Serial.print("0"); \
  if (ms_part < 10) Serial.print("0"); \
  Serial.print(ms_part); \
  Serial.print("] "); \
  Serial.print(category); \
  Serial.print(": "); \
} while(0)

// Main debug macro - only prints if DEBUG_ENABLED is true
#define DEBUG_PRINT(category, enabled, fmt, ...) do { \
  if (DEBUG_ENABLED && (enabled)) { \
    PRINT_TIME_HEADER(category); \
    Serial.printf(fmt, ##__VA_ARGS__); \
    Serial.println(); \
  } \
} while(0)

// Simpler macros for common categories
#define DEBUG_SHOT_PRINT(fmt, ...) DEBUG_PRINT("[SHOT]", DEBUG_SHOT, fmt, ##__VA_ARGS__)
#define DEBUG_SENSOR_PRINT(fmt, ...) DEBUG_PRINT("[SENSOR]", DEBUG_SENSOR, fmt, ##__VA_ARGS__)
#define DEBUG_BUTTON_PRINT(fmt, ...) DEBUG_PRINT("[BUTTON]", DEBUG_BUTTON, fmt, ##__VA_ARGS__)
#define DEBUG_ENCODER_PRINT(fmt, ...) DEBUG_PRINT("[ENCODER]", DEBUG_ENCODER, fmt, ##__VA_ARGS__)
#define DEBUG_SCALE_PRINT(fmt, ...) DEBUG_PRINT("[SCALE]", DEBUG_SCALE, fmt, ##__VA_ARGS__)
#define DEBUG_PUMP_PRINT(fmt, ...) DEBUG_PRINT("[PUMP]", DEBUG_PUMP, fmt, ##__VA_ARGS__)
#define DEBUG_STARTUP_PRINT(fmt, ...) DEBUG_PRINT("[STARTUP]", DEBUG_STARTUP, fmt, ##__VA_ARGS__)
#define DEBUG_STATE_PRINT(fmt, ...) DEBUG_PRINT("[STATE]", DEBUG_STATE, fmt, ##__VA_ARGS__)

// Macros for multiple parameters with named output
#define DEBUG_SHOT_VAL(name, value) DEBUG_SHOT_PRINT("%s: %.2f", name, (double)(value))
#define DEBUG_SENSOR_VAL(name, value) DEBUG_SENSOR_PRINT("%s: %.2f", name, (double)(value))
#define DEBUG_BUTTON_VAL(name, value) DEBUG_BUTTON_PRINT("%s: %d", name, (int)(value))
#define DEBUG_ENCODER_VAL(name, value) DEBUG_ENCODER_PRINT("%s: %ld", name, (long)(value))

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Get elapsed time in seconds since startup
inline float getElapsedSeconds() {
  return millis() / 1000.0;
}

// Get elapsed time in milliseconds since startup
inline unsigned long getElapsedMillis() {
  return millis();
}

#endif // DEBUG_H
