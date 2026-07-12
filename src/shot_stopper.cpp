#include "shot_stopper.h"

#include "cleaning_cycle.h"
#include "debug.h"
#include "settings.h"
#include "shot_history.h"

// ============================================================================
// SHARED STATE DEFINITIONS
// ============================================================================

AcaiaArduinoBLE scale(DEBUGMODE_ACAIA);

volatile bool scaleConnected = false;
volatile bool scaleNewWeight = false;
volatile bool scaleStartSequenceRequest = false;
volatile bool scaleStopTimerRequest = false;
volatile bool scaleTareRequest = false;
volatile float currentWeight = 0.0f;

const int BUTTON_INPUT_PIN = REEDSWITCH ? REED_IN : BUTTON_READ_PIN;

bool buttonLatched = false;

Shot shot = {
  0,     // startTimestampS
  0,     // shotTimer
  0,     // endS
  0,     // expectedEndS
  {},    // weight
  {},    // timeS
  {},    // pressureTrace
  0,     // datapoints
  false, // brewing
  EndType::UNDEF, // end
  0,     // pressure
  // pressureGoalByTime
  {
    {0.0f, 2.0f},   // 0s: 2 bar
    {5.0f, 9.0f},   // 5s: 9 bar
    {20.0f, 6.0f}   // 20s: 6 bar
  },
  3,     // numPressureGoalsByTime
  // pressureGoalByTimeLeft
  {
    {5.0f, 4.0f}    // 5s left: 4 bar
  },
  1,     // numPressureGoalsByTimeLeft
  0,     // currentGoalPressure
  0,     // goalWeight (set from EEPROM later)
  0,     // weightOffset (set from EEPROM later)
  255,   // pumpPwm (idle = full speed)
  0,     // peakPressure
  0      // pumpFlow
};

// ============================================================================
// BUTTON STATE (internal to the state machine)
// ============================================================================

#define BUTTON_STATE_ARRAY_LENGTH 8

enum ButtonState { IDLE, PRESSED, HELD, RELEASED };

static ButtonState buttonState = IDLE;
static int buttonReadings[BUTTON_STATE_ARRAY_LENGTH]; // Last raw button samples
static unsigned long lastButtonReadMs = 0;
static int newButtonState = 0;

// ============================================================================
// HELPERS
// ============================================================================

float secondsSinceBoot() {
  return millis() / 1000.0f;
}

const char* endReasonName(EndType end) {
  switch (end) {
    case EndType::BUTTON: return "BUTTON";
    case EndType::WEIGHT: return "WEIGHT";
    case EndType::TIME:   return "TIME";
    case EndType::WEB:    return "WEB";
    default:              return "UNDEF";
  }
}

float pressureBarFromVoltage(float voltage) {
  if (voltage < 0.4f) {
    return 0.0f;
  }
  if (voltage > 3.3f) {
    return 16.0f;
  }
  return (voltage - 0.4f) * (16.0f / (3.3f - 0.4f));
}

void updatePressureSensor(Shot* s) {
  int raw = analogRead(PRESSURE_PIN);
  float voltage = raw * (3.3f / 4095.0f);

  // EMA-smoothed: called every control iteration (~50 ms), so the filter is
  // always warm and the PID never sees raw ADC noise
  static float filtered = 0.0f;
  filtered += PRESSURE_FILTER_ALPHA * (pressureBarFromVoltage(voltage) - filtered);
  s->pressure = filtered;

  if (s->brewing && s->pressure > s->peakPressure) {
    s->peakPressure = s->pressure;
  }
  DEBUG_SENSOR_PRINT("Pressure: %.2f bar", s->pressure);
}

// ============================================================================
// SHOT LIFECYCLE
// ============================================================================

// Predict the shot end time by fitting a line (y = mx + b) to the last
// TREND_LINE_DATAPOINTS weight measurements and solving for the goal weight.
static void calculateEndTime(Shot* s) {
  // Do not predict end time if there aren't enough espresso measurements yet
  if ((s->datapoints < TREND_LINE_DATAPOINTS) || (s->weight[s->datapoints - 1] < 10)) {
    s->expectedEndS = MAX_SHOT_DURATION_S;
    return;
  }

  float sumXY = 0, sumX = 0, sumY = 0, sumSquaredX = 0;
  for (int i = s->datapoints - TREND_LINE_DATAPOINTS; i < s->datapoints; i++) {
    sumXY += s->timeS[i] * s->weight[i];
    sumX += s->timeS[i];
    sumY += s->weight[i];
    sumSquaredX += s->timeS[i] * s->timeS[i];
  }

  float m = (TREND_LINE_DATAPOINTS * sumXY - sumX * sumY)
            / (TREND_LINE_DATAPOINTS * sumSquaredX - sumX * sumX);
  float meanX = sumX / TREND_LINE_DATAPOINTS;
  float meanY = sumY / TREND_LINE_DATAPOINTS;
  float b = meanY - m * meanX;

  // Time at which the goal weight will be reached: x = (y - b) / m
  s->expectedEndS = (s->goalWeight - s->weightOffset - b) / m;
}

void setBrewingState(bool brewing) {
  if (brewing) {
    DEBUG_SHOT_PRINT("Shot started");
    shot.startTimestampS = secondsSinceBoot();
    shot.shotTimer = 0;
    shot.datapoints = 0;
    shot.peakPressure = 0;
    scaleStartSequenceRequest = true; // Scale task: resetTimer + startTimer (+ tare)
  } else {
    DEBUG_SHOT_PRINT("Shot ended by: %s (duration: %.1f s)",
                     endReasonName(shot.end), secondsSinceBoot() - shot.startTimestampS);

    shot.endS = secondsSinceBoot() - shot.startTimestampS;

    // Snapshot the trajectory into the history ring buffer before the next
    // shot overwrites it. Skip flushes shorter than MIN_SHOT_DURATION_S.
    if (shot.endS >= MIN_SHOT_DURATION_S) {
      recordShot(shot.timeS, shot.weight, shot.pressureTrace, shot.datapoints,
                 shot.endS, shot.peakPressure, (int)shot.end);
    }

    scaleStopTimerRequest = true;
    if (MOMENTARY &&
        (EndType::WEIGHT == shot.end || EndType::TIME == shot.end || EndType::WEB == shot.end)) {
      // Pulse the machine button to stop brewing
      DEBUG_SHOT_PRINT("Writing solenoid HIGH");
      digitalWrite(PRESS_BUTTON_PIN, HIGH);
      delay(1000);
      DEBUG_SHOT_PRINT("Writing solenoid LOW");
      digitalWrite(PRESS_BUTTON_PIN, LOW);
    } else if (!MOMENTARY) {
      buttonLatched = false;
      DEBUG_SHOT_PRINT("Button unlatched and not pressed");
      digitalWrite(PRESS_BUTTON_PIN, LOW);
    }
  }

  shot.end = EndType::UNDEF;
}

void updateShotTrajectory(Shot* s, float weight) {
  if (!s->brewing || s->datapoints >= MAX_SHOT_DATAPOINTS) {
    return;
  }

  // s->pressure is sampled every control iteration (main.cpp); this just
  // snapshots the latest filtered value at the scale's datapoint rate
  s->timeS[s->datapoints] = secondsSinceBoot() - s->startTimestampS;
  s->weight[s->datapoints] = weight;
  s->pressureTrace[s->datapoints] = s->pressure;
  s->shotTimer = s->timeS[s->datapoints];
  s->datapoints++;

  // Get the likely end time of the shot
  calculateEndTime(s);

  // Pressure goal: latest by-time goal that has been reached...
  float goalPressure = 0.0f;
  for (int i = 0; i < s->numPressureGoalsByTime; i++) {
    if (s->shotTimer >= s->pressureGoalByTime[i].timeS) {
      goalPressure = s->pressureGoalByTime[i].pressure;
    }
  }
  // ...overridden by any by-time-left goal that applies
  float timeLeft = s->expectedEndS - s->shotTimer;
  for (int i = 0; i < s->numPressureGoalsByTimeLeft; i++) {
    if (timeLeft <= s->pressureGoalByTimeLeft[i].timeLeftS) {
      goalPressure = s->pressureGoalByTimeLeft[i].pressure;
    }
  }
  s->currentGoalPressure = goalPressure;

  DEBUG_SHOT_PRINT("Time: %.1f s | Weight: %.1f g | Expected end: %.1f s | Goal weight: %.0f g | Pump: %s | Goal pressure: %.1f bar | Current pressure: %.1f bar",
    s->shotTimer,
    s->weight[s->datapoints - 1],
    s->expectedEndS,
    s->goalWeight,
    (s->pressure < goalPressure) ? "ON" : "OFF",
    goalPressure,
    s->pressure
  );
}

void handleMaxDurationReached(Shot* s) {
  if (s->brewing && s->shotTimer > MAX_SHOT_DURATION_S) {
    s->brewing = false;
    DEBUG_SHOT_PRINT("Max brew duration reached (%.1f s > %.1f s)",
                     s->shotTimer, (float)MAX_SHOT_DURATION_S);
    s->end = EndType::TIME;
    setBrewingState(s->brewing);
  }
}

void handleShotEnd(Shot* s, float weight) {
  if (s->brewing
      && s->shotTimer >= s->expectedEndS
      && s->shotTimer > MIN_SHOT_DURATION_S) {
    DEBUG_SHOT_PRINT("Goal weight achieved (%.1f g >= %.1f g - %.1f g offset)",
                     weight, s->goalWeight, s->weightOffset);
    s->brewing = false;
    s->end = EndType::WEIGHT;
    setBrewingState(s->brewing);
  }
}

void detectShotError(Shot* s, float weight) {
  if (s->startTimestampS
      && s->endS
      && weight >= (s->goalWeight - s->weightOffset)
      && secondsSinceBoot() > s->startTimestampS + s->endS + DRIP_DELAY_S) {
    s->startTimestampS = 0;
    s->endS = 0;

    if (abs(weight - s->goalWeight + s->weightOffset) > MAX_OFFSET) {
      DEBUG_SHOT_PRINT("Weight error detected: final=%.1f g, goal=%.0f g, offset=%.1f g - Error too large, offset unchanged",
        weight, s->goalWeight, s->weightOffset);
    } else {
      float newOffset = s->weightOffset + weight - s->goalWeight;
      DEBUG_SHOT_PRINT("Weight correction: final=%.1f g, goal=%.0f g, old_offset=%.1f g → new_offset=%.1f g",
        weight, s->goalWeight, s->weightOffset, newOffset);
      s->weightOffset = newOffset;

      settingsSave();
      DEBUG_SHOT_PRINT("New offset saved to EEPROM");
    }
  }
}

// ============================================================================
// BUTTON STATE MACHINE
// ============================================================================

void handleButtonLogic() {
  // Sample the button every BUTTON_READ_PERIOD_MS
  if (millis() > (lastButtonReadMs + BUTTON_READ_PERIOD_MS)) {
    lastButtonReadMs = millis();

    // Shift the sample array and add the newest reading
    for (int i = BUTTON_STATE_ARRAY_LENGTH - 2; i >= 0; i--) {
      buttonReadings[i + 1] = buttonReadings[i];
    }
    buttonReadings[0] = digitalRead(BUTTON_INPUT_PIN); // Active low

    // Debounce: pressed if any recent sample is set
    newButtonState = 0;
    for (int i = 0; i < BUTTON_STATE_ARRAY_LENGTH; i++) {
      if (buttonReadings[i]) {
        newButtonState = 1;
      }
    }

    // Handle reed switch noise
    if (REEDSWITCH && !shot.brewing && secondsSinceBoot() < (shot.startTimestampS + shot.endS + 0.5)) {
      newButtonState = 0;
    }
  }

  switch (buttonState) {
    case IDLE:
      if (newButtonState) {
        DEBUG_BUTTON_PRINT("Button pressed");
        buttonState = PRESSED;
        if (!MOMENTARY) {
          if (cleaningActive()) {
            // The user toggled the machine themselves - abort cleaning, do
            // not start a shot on top of it
            cleaningAbortFromButton();
          } else {
            shot.brewing = true;
            setBrewingState(shot.brewing);
          }
        }
      }
      break;

    case PRESSED:
      if (!newButtonState) {
        DEBUG_BUTTON_PRINT("Button released");
        buttonState = RELEASED;
        if (MOMENTARY && cleaningActive()) {
          // Physical press during cleaning = abort; machine state already
          // changed by the user's press, so only our tracking is released
          cleaningAbortFromButton();
          break;
        }
        shot.brewing = !shot.brewing;
        if (!shot.brewing) {
          shot.end = EndType::BUTTON;
        }
        setBrewingState(shot.brewing);
      } else if (!MOMENTARY && shot.brewing && !buttonLatched && (shot.shotTimer > MIN_SHOT_DURATION_S)) {
        DEBUG_BUTTON_PRINT("Button latched");
        buttonState = HELD;
        buttonLatched = true;
        DEBUG_BUTTON_PRINT("Writing solenoid HIGH");
        digitalWrite(PRESS_BUTTON_PIN, HIGH);
        if (AUTOTARE) {
          scaleTareRequest = true;
        }
      }
      break;

    case HELD:
      if (newButtonState) {
        DEBUG_BUTTON_PRINT("Button released");
        buttonState = RELEASED;
        shot.brewing = false;
        shot.end = EndType::BUTTON;
        setBrewingState(shot.brewing);
      }
      break;

    case RELEASED:
      if (!newButtonState) {
        buttonState = IDLE;
      }
      break;
  }
}
