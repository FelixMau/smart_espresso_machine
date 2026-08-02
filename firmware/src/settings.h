#ifndef SETTINGS_H
#define SETTINGS_H

// ============================================================================
// PERSISTENT SETTINGS (EEPROM)
// ============================================================================
// One versioned blob holding everything that should survive a reboot:
// goal weight, learned weight offset, the pressure profile, the cleaning
// cycle configuration and optional WiFi credentials. Stored with EEPROM.put
// at SETTINGS_ADDR; the two legacy single-byte slots (goal weight at byte 0,
// offset x10 at byte 1) are read once for migration when no blob exists yet.
//
// settingsLoad() runs in setup() before the FreeRTOS tasks start: it reads
// and validates the blob (or migrates/derives defaults) and applies it to the
// live state (shot, cleaningConfig). settingsSave() snapshots the live state
// back into the blob and commits - call it after any change worth keeping.

#include <Arduino.h>

#include "cleaning_cycle.h"
#include "shot_stopper.h"

struct PersistentSettings {
  uint32_t magic;    // SETTINGS_MAGIC when the blob is valid
  uint8_t version;   // Bump on any layout change (no cross-version migration)

  // Brewing
  float goalWeight;
  float weightOffset;

  // Pressure profile (same shape as the live arrays in Shot)
  uint8_t numGoalsByTime;
  uint8_t numGoalsByTimeLeft;
  PressureGoalByTime goalsByTime[MAX_PRESSURE_GOALS];
  PressureGoalByTimeLeft goalsByTimeLeft[MAX_PRESSURE_GOALS];

  // Cleaning cycle
  CleaningConfig cleaning;

  // WiFi. Empty ssid = use the compile-time credentials from secrets.h
  // (that fallback lives in webserver.cpp - this module never sees secrets.h,
  // whose ssid/password macros would clobber these field names)
  char wifiSsid[33];      // 32 chars max per 802.11 + NUL
  char wifiPassword[65];  // 64 chars max WPA2 passphrase + NUL
};

// The persisted state. WiFi fields are authoritative here; everything else
// is a snapshot of the live state taken by settingsSave().
extern PersistentSettings settings;

// Read + validate the blob from EEPROM (migrating the legacy two-byte layout
// if none exists) and apply it to shot and cleaningConfig. Call once in
// setup(), before the control task starts.
void settingsLoad();

// Snapshot live state (shot goals/profile, cleaningConfig) into the blob and
// commit to EEPROM. Safe to call from any task (internally serialized).
void settingsSave();

// Store new WiFi credentials (applied on next boot). Empty SSID reverts to
// the compile-time secrets.h credentials. Parameter names must not be
// ssid/password: secrets.h defines those as macros and webserver.cpp
// includes both headers.
void settingsSetWifi(const char* newSsid, const char* newPass);

#endif // SETTINGS_H
