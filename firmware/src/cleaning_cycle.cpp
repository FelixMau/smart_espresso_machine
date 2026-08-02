#include "cleaning_cycle.h"

#include "debug.h"
#include "shot_stopper.h"

// ============================================================================
// SHARED STATE DEFINITIONS
// ============================================================================

// Defaults follow the standard detergent backflush (see cleaning_cycle.h):
// 5 flushes per phase, 10 s vent pause, 60 s soak. The 9 bar fill limit
// matches normal brew pressure - more buys no cleaning power, it only
// stresses the pump and group gasket.
CleaningConfig cleaningConfig = {
  9.0f,   // maxPressureBar
  10.0f,  // fillTimeoutS (classic routines run the pump 10 s)
  5.0f,   // holdS
  120,    // holdPumpLevel (~47%: keeps pressure without full deadhead)
  10.0f,  // pauseS
  5,      // cyclesPerPhase
  60.0f,  // soakS
  600.0f  // awaitUserTimeoutS
};

volatile bool cleaningStartRequest = false;
volatile bool cleaningContinueRequest = false;
volatile bool cleaningStopRequest = false;

// ============================================================================
// INTERNAL STATE (control task only)
// ============================================================================

static CleaningPhase phase = CleaningPhase::NONE;
static CleaningState state = CleaningState::OFF;
static int cycleIdx = 0;          // 0-based flush index within the phase
static float stateStartS = 0;
static bool machineOn = false;    // Whether WE believe the brew circuit is on
static float fillPeakBar = 0;     // Peak pressure of the fill in progress
static float lastFillPeakBar = 0;
static bool lastFillReachedMax = false;

// ============================================================================
// MACHINE BUTTON CONTROL
// ============================================================================
// Same electrical pattern as the web start/stop in main.cpp: momentary
// machines get a 1 s toggle pulse, latching machines a held level.

static void setMachine(bool on) {
  if (on == machineOn) {
    return;
  }
  if (MOMENTARY) {
    digitalWrite(PRESS_BUTTON_PIN, HIGH);
    delay(1000);
    digitalWrite(PRESS_BUTTON_PIN, LOW);
  } else {
    digitalWrite(PRESS_BUTTON_PIN, on ? HIGH : LOW);
    buttonLatched = on;
  }
  machineOn = on;
}

static void enterState(CleaningState s) {
  state = s;
  stateStartS = secondsSinceBoot();
}

static void deactivate() {
  phase = CleaningPhase::NONE;
  state = CleaningState::OFF;
  cycleIdx = 0;
  machineOn = false;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool cleaningActive() {
  return phase == CleaningPhase::DETERGENT
      || phase == CleaningPhase::SOAK
      || phase == CleaningPhase::AWAIT_RINSE
      || phase == CleaningPhase::RINSE;
}

uint8_t cleaningPumpLevel() {
  if (state == CleaningState::HOLD) {
    return cleaningConfig.holdPumpLevel;
  }
  return 255;
}

void cleaningAbortFromButton() {
  if (!cleaningActive()) {
    return;
  }
  DEBUG_CLEANING_PRINT("Physical button press - cleaning aborted, machine left as the user set it");
  // The user has already toggled the real machine; releasing our tracking
  // without pulsing keeps us from toggling it right back
  if (!MOMENTARY) {
    buttonLatched = false;
    digitalWrite(PRESS_BUTTON_PIN, LOW);
  }
  deactivate();
}

const char* cleaningPhaseName() {
  switch (phase) {
    case CleaningPhase::DETERGENT:   return "detergent";
    case CleaningPhase::SOAK:        return "soak";
    case CleaningPhase::AWAIT_RINSE: return "await_rinse";
    case CleaningPhase::RINSE:       return "rinse";
    case CleaningPhase::DONE:        return "done";
    default:                         return "none";
  }
}

const char* cleaningStateName() {
  switch (state) {
    case CleaningState::PRESSURIZE: return "pressurize";
    case CleaningState::HOLD:       return "hold";
    case CleaningState::RELEASE:    return "release";
    default:                        return "off";
  }
}

int cleaningCurrentCycle() {
  return cycleIdx + 1;
}

float cleaningStateElapsedS() {
  if (state == CleaningState::OFF && phase != CleaningPhase::SOAK
      && phase != CleaningPhase::AWAIT_RINSE) {
    return 0;
  }
  return secondsSinceBoot() - stateStartS;
}

float cleaningLastFillPeakBar() {
  return lastFillPeakBar;
}

bool cleaningLastFillReachedMax() {
  return lastFillReachedMax;
}

// ============================================================================
// STATE MACHINE
// ============================================================================

static void startFlush() {
  fillPeakBar = 0;
  setMachine(true);
  enterState(CleaningState::PRESSURIZE);
  DEBUG_CLEANING_PRINT("%s flush %d/%d: pressurizing (limit %.1f bar, timeout %.0f s)",
                       cleaningPhaseName(), cycleIdx + 1, cleaningConfig.cyclesPerPhase,
                       cleaningConfig.maxPressureBar, cleaningConfig.fillTimeoutS);
}

// A flush's off-pause finished: next flush, or end of phase
static void advanceAfterRelease() {
  cycleIdx++;
  if (cycleIdx < cleaningConfig.cyclesPerPhase) {
    startFlush();
    return;
  }
  if (phase == CleaningPhase::DETERGENT) {
    phase = CleaningPhase::SOAK;
    state = CleaningState::OFF;
    stateStartS = secondsSinceBoot();
    DEBUG_CLEANING_PRINT("Detergent flushes done - soaking %.0f s", cleaningConfig.soakS);
  } else {
    DEBUG_CLEANING_PRINT("Rinse flushes done - cleaning cycle complete");
    deactivate();
    phase = CleaningPhase::DONE;
  }
}

static void endFill(bool reachedMax) {
  lastFillPeakBar = fillPeakBar;
  lastFillReachedMax = reachedMax;
  if (!reachedMax) {
    DEBUG_CLEANING_PRINT("Fill timeout at %.1f bar (< %.1f bar) - blind basket inserted?",
                         fillPeakBar, cleaningConfig.maxPressureBar);
    // Water is simply flowing through; skip the hold and release directly
    setMachine(false);
    enterState(CleaningState::RELEASE);
    return;
  }
  DEBUG_CLEANING_PRINT("Max pressure %.1f bar reached - holding %.1f s at pump level %d",
                       fillPeakBar, cleaningConfig.holdS, cleaningConfig.holdPumpLevel);
  enterState(CleaningState::HOLD);
}

void cleaningUpdate(float pressureBar) {
  // ------------------------------------------------------------------ requests
  if (cleaningStopRequest) {
    cleaningStopRequest = false;
    if (cleaningActive()) {
      DEBUG_CLEANING_PRINT("Cleaning aborted via web");
      setMachine(false);
      deactivate();
    }
  }
  if (cleaningStartRequest) {
    cleaningStartRequest = false;
    if (!cleaningActive() && !shot.brewing) {
      DEBUG_CLEANING_PRINT("Cleaning cycle started: %d flushes, max %.1f bar, %.0f s soak",
                           cleaningConfig.cyclesPerPhase, cleaningConfig.maxPressureBar,
                           cleaningConfig.soakS);
      phase = CleaningPhase::DETERGENT;
      cycleIdx = 0;
      lastFillPeakBar = 0;
      lastFillReachedMax = false;
      startFlush();
    }
  }
  if (cleaningContinueRequest) {
    cleaningContinueRequest = false;
    if (phase == CleaningPhase::AWAIT_RINSE) {
      DEBUG_CLEANING_PRINT("Rinse confirmed - starting %d water flushes",
                           cleaningConfig.cyclesPerPhase);
      phase = CleaningPhase::RINSE;
      cycleIdx = 0;
      startFlush();
    }
  }

  if (!cleaningActive()) {
    return;
  }

  float elapsed = secondsSinceBoot() - stateStartS;

  // ------------------------------------------------------- overpressure failsafe
  if (machineOn && pressureBar > cleaningConfig.maxPressureBar + CLEANING_HARD_MARGIN_BAR) {
    DEBUG_CLEANING_PRINT("OVERPRESSURE %.1f bar - releasing immediately", pressureBar);
    lastFillPeakBar = max(fillPeakBar, pressureBar);
    lastFillReachedMax = true;
    setMachine(false);
    enterState(CleaningState::RELEASE);
    return;
  }

  switch (state) {
    case CleaningState::PRESSURIZE:
      if (pressureBar > fillPeakBar) {
        fillPeakBar = pressureBar;
      }
      if (pressureBar >= cleaningConfig.maxPressureBar) {
        endFill(true);
      } else if (elapsed > cleaningConfig.fillTimeoutS) {
        endFill(false);
      }
      break;

    case CleaningState::HOLD:
      if (pressureBar > fillPeakBar) {
        fillPeakBar = pressureBar;
        lastFillPeakBar = fillPeakBar;
      }
      if (elapsed > cleaningConfig.holdS) {
        DEBUG_CLEANING_PRINT("Releasing - valve vents detergent to drip tray, pausing %.0f s",
                             cleaningConfig.pauseS);
        setMachine(false);
        enterState(CleaningState::RELEASE);
      }
      break;

    case CleaningState::RELEASE:
      if (elapsed > cleaningConfig.pauseS) {
        advanceAfterRelease();
      }
      break;

    case CleaningState::OFF:
      if (phase == CleaningPhase::SOAK && elapsed > cleaningConfig.soakS) {
        phase = CleaningPhase::AWAIT_RINSE;
        stateStartS = secondsSinceBoot();
        DEBUG_CLEANING_PRINT("Soak done - remove portafilter, rinse blind basket, then confirm rinse phase via dashboard");
      } else if (phase == CleaningPhase::AWAIT_RINSE
                 && elapsed > cleaningConfig.awaitUserTimeoutS) {
        DEBUG_CLEANING_PRINT("No rinse confirmation after %.0f s - cleaning aborted",
                             cleaningConfig.awaitUserTimeoutS);
        deactivate();
      }
      break;
  }
}
