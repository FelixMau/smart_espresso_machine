#include "pressure_profile.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE profileMux = portMUX_INITIALIZER_UNLOCKED;

static bool fail(String* error, const String& message) {
  if (error) {
    *error = message;
  }
  return false;
}

bool pressureProfilePressureIsSafe(float pressureBar) {
  return isfinite(pressureBar) && pressureBar >= 0.0f &&
         pressureBar <= MAX_PROFILE_PRESSURE_BAR;
}

static bool parseStrictFloat(const String& token, float* value) {
  if (!value) {
    return false;
  }

  String trimmed = token;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return false;
  }

  errno = 0;
  const char* start = trimmed.c_str();
  char* end = nullptr;
  float parsed = strtof(start, &end);
  if (end == start || *end != '\0' || errno == ERANGE || !isfinite(parsed)) {
    return false;
  }

  *value = parsed;
  return true;
}

static bool nextCsvToken(const String& input, int* position, bool* finished,
                         String* token) {
  if (!position || !finished || !token || *finished) {
    return false;
  }

  int comma = input.indexOf(',', *position);
  if (comma < 0) {
    *token = input.substring(*position);
    *finished = true;
  } else {
    *token = input.substring(*position, comma);
    *position = comma + 1;
  }
  return true;
}

bool pressureProfileIsValid(const PressureProfile& profile, String* error) {
  if (profile.numByTime < 1 || profile.numByTime > MAX_PRESSURE_GOALS) {
    return fail(error, "profile needs 1-8 elapsed-time goals");
  }
  if (profile.numByTimeLeft < 0 || profile.numByTimeLeft > MAX_PRESSURE_GOALS) {
    return fail(error, "profile has too many time-left goals");
  }

  for (int i = 0; i < profile.numByTime; i++) {
    const PressureGoalByTime& goal = profile.byTime[i];
    if (!isfinite(goal.timeS) || goal.timeS < 0.0f ||
        goal.timeS > MAX_SHOT_DURATION_S) {
      return fail(error, "elapsed time is outside the shot window");
    }
    if (!pressureProfilePressureIsSafe(goal.pressure)) {
      return fail(error, "pressure is outside the safe range");
    }
    if (i == 0 && goal.timeS != 0.0f) {
      return fail(error, "profile must start with an elapsed-time goal at 0s");
    }
    if (i > 0 && goal.timeS <= profile.byTime[i - 1].timeS) {
      return fail(error, "elapsed-time goals must be strictly increasing");
    }
  }

  for (int i = 0; i < profile.numByTimeLeft; i++) {
    const PressureGoalByTimeLeft& goal = profile.byTimeLeft[i];
    if (!isfinite(goal.timeLeftS) || goal.timeLeftS <= 0.0f ||
        goal.timeLeftS > MAX_SHOT_DURATION_S) {
      return fail(error, "time-left value is outside the shot window");
    }
    if (!pressureProfilePressureIsSafe(goal.pressure)) {
      return fail(error, "pressure is outside the safe range");
    }
    if (i > 0 && goal.timeLeftS >= profile.byTimeLeft[i - 1].timeLeftS) {
      return fail(error, "time-left goals must be strictly decreasing");
    }
  }

  return true;
}

bool parsePressureProfile(const String& times, const String& pressures,
                          PressureProfile* profile, String* error) {
  if (!profile) {
    return fail(error, "internal profile error");
  }

  PressureProfile parsed = {};
  int timePosition = 0;
  int pressurePosition = 0;
  bool timesFinished = false;
  bool pressuresFinished = false;
  int entry = 0;

  while (true) {
    String timeToken;
    String pressureToken;
    bool hasTime = nextCsvToken(times, &timePosition, &timesFinished, &timeToken);
    bool hasPressure = nextCsvToken(pressures, &pressurePosition,
                                    &pressuresFinished, &pressureToken);
    if (hasTime != hasPressure) {
      return fail(error, "times and pressures must contain the same number of values");
    }
    if (!hasTime) {
      break;
    }

    float timeValue = 0.0f;
    float pressureValue = 0.0f;
    if (!parseStrictFloat(timeToken, &timeValue)) {
      return fail(error, String("invalid time at entry ") + (entry + 1));
    }
    if (!parseStrictFloat(pressureToken, &pressureValue)) {
      return fail(error, String("invalid pressure at entry ") + (entry + 1));
    }
    if (!pressureProfilePressureIsSafe(pressureValue)) {
      return fail(error, String("pressure out of range at entry ") + (entry + 1));
    }
    if (fabsf(timeValue) > MAX_SHOT_DURATION_S) {
      return fail(error, String("time out of range at entry ") + (entry + 1));
    }

    if (timeValue < 0.0f) {
      if (parsed.numByTimeLeft >= MAX_PRESSURE_GOALS) {
        return fail(error, "at most 8 time-left goals are allowed");
      }
      parsed.byTimeLeft[parsed.numByTimeLeft++] = {-timeValue, pressureValue};
    } else {
      if (parsed.numByTime >= MAX_PRESSURE_GOALS) {
        return fail(error, "at most 8 elapsed-time goals are allowed");
      }
      parsed.byTime[parsed.numByTime++] = {timeValue, pressureValue};
    }
    entry++;
  }

  if (!pressureProfileIsValid(parsed, error)) {
    return false;
  }

  *profile = parsed;
  return true;
}

void pressureProfileSnapshot(const Shot& source, PressureProfile* profile) {
  if (!profile) {
    return;
  }

  portENTER_CRITICAL(&profileMux);
  memcpy(profile->byTime, source.pressureGoalByTime, sizeof(profile->byTime));
  memcpy(profile->byTimeLeft, source.pressureGoalByTimeLeft,
         sizeof(profile->byTimeLeft));
  profile->numByTime = source.numPressureGoalsByTime;
  profile->numByTimeLeft = source.numPressureGoalsByTimeLeft;
  portEXIT_CRITICAL(&profileMux);

  // Keep every snapshot safe to iterate even if a future caller corrupts a
  // live count without going through pressureProfileApply().
  if (profile->numByTime < 0 || profile->numByTime > MAX_PRESSURE_GOALS ||
      profile->numByTimeLeft < 0 ||
      profile->numByTimeLeft > MAX_PRESSURE_GOALS) {
    memset(profile, 0, sizeof(*profile));
  }
}

bool pressureProfileApply(Shot* destination, const PressureProfile& profile) {
  if (!destination || !pressureProfileIsValid(profile)) {
    return false;
  }

  portENTER_CRITICAL(&profileMux);
  destination->numPressureGoalsByTime = 0;
  destination->numPressureGoalsByTimeLeft = 0;
  memset(destination->pressureGoalByTime, 0,
         sizeof(destination->pressureGoalByTime));
  memset(destination->pressureGoalByTimeLeft, 0,
         sizeof(destination->pressureGoalByTimeLeft));
  memcpy(destination->pressureGoalByTime, profile.byTime,
         profile.numByTime * sizeof(profile.byTime[0]));
  memcpy(destination->pressureGoalByTimeLeft, profile.byTimeLeft,
         profile.numByTimeLeft * sizeof(profile.byTimeLeft[0]));
  destination->numPressureGoalsByTime = profile.numByTime;
  destination->numPressureGoalsByTimeLeft = profile.numByTimeLeft;
  portEXIT_CRITICAL(&profileMux);
  return true;
}

float pressureProfileGoalAt(const Shot& source, float shotTimer, float timeLeft) {
  PressureProfile profile = {};
  pressureProfileSnapshot(source, &profile);
  if (!isfinite(shotTimer) || !isfinite(timeLeft) ||
      !pressureProfileIsValid(profile)) {
    return 0.0f;
  }

  float goalPressure = 0.0f;
  for (int i = 0; i < profile.numByTime; i++) {
    if (shotTimer >= profile.byTime[i].timeS) {
      goalPressure = profile.byTime[i].pressure;
    }
  }
  for (int i = 0; i < profile.numByTimeLeft; i++) {
    if (timeLeft <= profile.byTimeLeft[i].timeLeftS) {
      goalPressure = profile.byTimeLeft[i].pressure;
    }
  }
  return goalPressure;
}
