#include "settings.h"

#include <EEPROM.h>

#include "debug.h"

// ============================================================================
// EEPROM LAYOUT
// ============================================================================

// Legacy layout (firmware before the settings blob): byte 0 = goal weight in
// grams, byte 1 = weight offset x10. Read once for migration only.
#define LEGACY_WEIGHT_ADDR 0
#define LEGACY_OFFSET_ADDR 1

#define SETTINGS_ADDR 4  // Blob starts past the legacy bytes
#define SETTINGS_EEPROM_SIZE 512
#define SETTINGS_MAGIC 0x45535052u  // "ESPR"
#define SETTINGS_VERSION 1

static_assert(SETTINGS_ADDR + sizeof(PersistentSettings) <= SETTINGS_EEPROM_SIZE,
              "PersistentSettings no longer fits the EEPROM region - grow SETTINGS_EEPROM_SIZE");

PersistentSettings settings = {};

// settingsSave() is called from both the control task (post-shot offset
// learning) and the AsyncTCP task (the /set_* handlers); the EEPROM library
// shares one RAM cache and dirty flag, so serialize the snapshot+commit.
static SemaphoreHandle_t settingsLock = nullptr;

// ============================================================================
// VALIDATION
// ============================================================================

static bool inRange(float v, float lo, float hi) {
  return !isnan(v) && v >= lo && v <= hi;
}

// Sanitize the blob in place (covers both a corrupted blob that still passed
// the magic check and out-of-range legacy migration values). Ranges mirror
// the /set_* handlers; cleaning fields fall back to the compiled-in defaults
// still present in cleaningConfig when this runs during boot.
static void validateSettings(const CleaningConfig& cleaningDefaults) {
  if (!inRange(settings.goalWeight, 10, 200)) {
    settings.goalWeight = 36;
    DEBUG_STARTUP_PRINT("Goal weight out of range, set to default: 36 g");
  }
  if (!inRange(settings.weightOffset, 0, MAX_OFFSET)) {
    settings.weightOffset = 1.5f;
    DEBUG_STARTUP_PRINT("Offset out of range, set to default: 1.5 g");
  }

  if (settings.numGoalsByTime > MAX_PRESSURE_GOALS) {
    settings.numGoalsByTime = 0;
  }
  if (settings.numGoalsByTimeLeft > MAX_PRESSURE_GOALS) {
    settings.numGoalsByTimeLeft = 0;
  }

  CleaningConfig& c = settings.cleaning;
  if (!inRange(c.maxPressureBar, 4, 12))      c.maxPressureBar = cleaningDefaults.maxPressureBar;
  if (!inRange(c.fillTimeoutS, 3, 60))        c.fillTimeoutS = cleaningDefaults.fillTimeoutS;
  if (!inRange(c.holdS, 0, 30))               c.holdS = cleaningDefaults.holdS;
  if (!inRange(c.pauseS, 2, 60))              c.pauseS = cleaningDefaults.pauseS;
  if (c.cyclesPerPhase < 1 || c.cyclesPerPhase > 10) c.cyclesPerPhase = cleaningDefaults.cyclesPerPhase;
  if (!inRange(c.soakS, 0, 600))              c.soakS = cleaningDefaults.soakS;
  if (!inRange(c.awaitUserTimeoutS, 30, 3600)) c.awaitUserTimeoutS = cleaningDefaults.awaitUserTimeoutS;

  // A truncated/corrupted blob must never yield unterminated strings
  settings.wifiSsid[sizeof(settings.wifiSsid) - 1] = '\0';
  settings.wifiPassword[sizeof(settings.wifiPassword) - 1] = '\0';
}

// ============================================================================
// LOAD / SAVE
// ============================================================================

// Write the blob without touching the live state; callers hold settingsLock
// (or run single-threaded during setup). EEPROM.commit() only hits flash if
// a byte actually changed, so calling this on every boot costs nothing.
static void commitBlob() {
  settings.magic = SETTINGS_MAGIC;
  settings.version = SETTINGS_VERSION;
  EEPROM.put(SETTINGS_ADDR, settings);
  EEPROM.commit();
}

void settingsLoad() {
  settingsLock = xSemaphoreCreateMutex();
  EEPROM.begin(SETTINGS_EEPROM_SIZE);

  // Compiled-in defaults, captured before anything overwrites the live state
  const CleaningConfig cleaningDefaults = cleaningConfig;

  EEPROM.get(SETTINGS_ADDR, settings);
  if (settings.magic != SETTINGS_MAGIC || settings.version != SETTINGS_VERSION) {
    // First boot with this layout: seed from the legacy two-byte slots (their
    // out-of-range/erased-flash values are caught by validateSettings) and
    // the compiled-in defaults for everything the old layout never stored
    DEBUG_STARTUP_PRINT("No settings blob found - migrating legacy EEPROM values and defaults");
    settings.goalWeight = EEPROM.read(LEGACY_WEIGHT_ADDR);
    settings.weightOffset = EEPROM.read(LEGACY_OFFSET_ADDR) / 10.0f;
    settings.numGoalsByTime = shot.numPressureGoalsByTime;
    settings.numGoalsByTimeLeft = shot.numPressureGoalsByTimeLeft;
    memcpy(settings.goalsByTime, shot.pressureGoalByTime, sizeof(settings.goalsByTime));
    memcpy(settings.goalsByTimeLeft, shot.pressureGoalByTimeLeft, sizeof(settings.goalsByTimeLeft));
    settings.cleaning = cleaningDefaults;
    settings.wifiSsid[0] = '\0';
    settings.wifiPassword[0] = '\0';
  }

  validateSettings(cleaningDefaults);

  // Apply to the live state (tasks aren't running yet, no locking needed)
  shot.goalWeight = settings.goalWeight;
  shot.weightOffset = settings.weightOffset;
  shot.numPressureGoalsByTime = settings.numGoalsByTime;
  shot.numPressureGoalsByTimeLeft = settings.numGoalsByTimeLeft;
  memcpy(shot.pressureGoalByTime, settings.goalsByTime, sizeof(shot.pressureGoalByTime));
  memcpy(shot.pressureGoalByTimeLeft, settings.goalsByTimeLeft, sizeof(shot.pressureGoalByTimeLeft));
  cleaningConfig = settings.cleaning;

  // Persist migration/sanitization results (no-op flash-wise if unchanged)
  commitBlob();

  DEBUG_STARTUP_PRINT("Settings loaded: goal %.0f g, offset %.1f g, profile %d+%d goals, WiFi '%s'",
                      settings.goalWeight, settings.weightOffset,
                      settings.numGoalsByTime, settings.numGoalsByTimeLeft,
                      settings.wifiSsid[0] ? settings.wifiSsid : "(compile-time)");
}

void settingsSave() {
  if (settingsLock && xSemaphoreTake(settingsLock, pdMS_TO_TICKS(500)) != pdTRUE) {
    DEBUG_STARTUP_PRINT("Settings save skipped - lock timeout");
    return;
  }

  settings.goalWeight = shot.goalWeight;
  settings.weightOffset = shot.weightOffset;
  settings.numGoalsByTime = shot.numPressureGoalsByTime;
  settings.numGoalsByTimeLeft = shot.numPressureGoalsByTimeLeft;
  memcpy(settings.goalsByTime, shot.pressureGoalByTime, sizeof(settings.goalsByTime));
  memcpy(settings.goalsByTimeLeft, shot.pressureGoalByTimeLeft, sizeof(settings.goalsByTimeLeft));
  settings.cleaning = cleaningConfig;
  commitBlob();

  if (settingsLock) {
    xSemaphoreGive(settingsLock);
  }
}

void settingsSetWifi(const char* newSsid, const char* newPass) {
  strlcpy(settings.wifiSsid, newSsid, sizeof(settings.wifiSsid));
  strlcpy(settings.wifiPassword, newPass, sizeof(settings.wifiPassword));
  settingsSave();
  DEBUG_STARTUP_PRINT("WiFi credentials stored for '%s' (used on next boot)",
                      settings.wifiSsid[0] ? settings.wifiSsid : "(compile-time)");
}
