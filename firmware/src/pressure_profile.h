#ifndef PRESSURE_PROFILE_H
#define PRESSURE_PROFILE_H

#include <Arduino.h>

#include "shot_stopper.h"

// A complete pressure profile that can be parsed and validated before it is
// published to the live Shot state.
struct PressureProfile {
  PressureGoalByTime byTime[MAX_PRESSURE_GOALS];
  PressureGoalByTimeLeft byTimeLeft[MAX_PRESSURE_GOALS];
  int numByTime;
  int numByTimeLeft;
};

// Parse the dashboard/API CSV representation. Parsing is all-or-nothing:
// malformed, non-finite, out-of-range, unordered, or mismatched input fails.
bool parsePressureProfile(const String& times, const String& pressures,
                          PressureProfile* profile, String* error);

// Validate a profile from any source, including EEPROM.
bool pressureProfileIsValid(const PressureProfile& profile, String* error = nullptr);

// Profile pressure invariant shared by persistence and the control fail-safe.
bool pressureProfilePressureIsSafe(float pressureBar);

// Coherent cross-task access to the profile embedded in Shot.
void pressureProfileSnapshot(const Shot& source, PressureProfile* profile);
bool pressureProfileApply(Shot* destination, const PressureProfile& profile);

// Evaluate a coherent snapshot using the profile's documented ordering.
float pressureProfileGoalAt(const Shot& source, float shotTimer, float timeLeft);

#endif // PRESSURE_PROFILE_H
