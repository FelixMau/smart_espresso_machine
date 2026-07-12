#ifndef CLEANING_CYCLE_H
#define CLEANING_CYCLE_H

// ============================================================================
// CLEANING CYCLE - AUTOMATED DETERGENT BACKFLUSH
// ============================================================================
// Automates the standard espresso-machine backflush with a blind basket and
// detergent (Cafiza/Biocaf style). Reference routines:
//   - La Marzocco manual backflush: 5x (10 s on / 10 s off) with detergent,
//     then the same again with plain water to rinse
//   - GS3 built-in auto backflush: 5 s on / 5 s pause per cycle
//   - Gaggiuino clean workflow: ~8-10 s pressurize, then release, repeated
//
// Difference here: the on-phase is PRESSURE-LIMITED instead of timed. The
// blind basket lets no water escape, so the pump deadheads against a closed
// volume - running it a fixed 10 s serves no purpose and stresses the pump.
// Instead each flush is:
//
//   PRESSURIZE  machine button ON, pump full power, until the MPX5500 reads
//               maxPressureBar (or fillTimeoutS as a fallback if pressure
//               never builds, e.g. blind basket forgotten)
//   HOLD        dwell holdS at pressure with the pump dimmed to holdPumpLevel
//               so the detergent works without deadheading the pump at 100%
//   RELEASE     machine button OFF - the 3-way valve vents the pressurized
//               detergent through the valve body into the drip tray (this
//               release is what actually scrubs the valve), pause pauseS
//
// Full cycle: user puts detergent in the blind basket and starts via web ->
// cyclesPerPhase detergent flushes -> soakS soak -> AWAIT_RINSE (user rinses
// the blind basket, confirms via web) -> cyclesPerPhase water flushes -> DONE.
//
// Failsafe: if pressure ever exceeds maxPressureBar + CLEANING_HARD_MARGIN_BAR
// while the machine is on, the button is released immediately.
//
// The cycle runs entirely inside the control task (cleaningUpdate() is called
// every control iteration); HTTP handlers only set the request flags below,
// mirroring the webStartRequest pattern.

#include <Arduino.h>

// Immediate release if pressure overshoots the configured max by this much
#define CLEANING_HARD_MARGIN_BAR 1.0f

enum class CleaningPhase { NONE, DETERGENT, SOAK, AWAIT_RINSE, RINSE, DONE };
enum class CleaningState { OFF, PRESSURIZE, HOLD, RELEASE };

// Live-tunable via /set_cleaning (RAM only, defaults restored on boot)
struct CleaningConfig {
  float maxPressureBar;   // End the fill when this pressure is reached
  float fillTimeoutS;     // Fallback fill duration if pressure never builds
  float holdS;            // Dwell at max pressure before releasing
  uint8_t holdPumpLevel;  // Dimmer level (0-255) while holding pressure
  float pauseS;           // Off-time between flushes (valve vents + scrubs)
  int cyclesPerPhase;     // Flushes per phase (detergent and rinse alike)
  float soakS;            // Soak after the detergent flushes
  float awaitUserTimeoutS;// Abort if the user never confirms the rinse phase
};

extern CleaningConfig cleaningConfig;

// Set by HTTP handlers, consumed by cleaningUpdate() in the control task
extern volatile bool cleaningStartRequest;
extern volatile bool cleaningContinueRequest;  // AWAIT_RINSE -> rinse flushes
extern volatile bool cleaningStopRequest;

// True from start until DONE/abort; blocks shot starts while cleaning
bool cleaningActive();

// Run one iteration of the cleaning state machine (control task, ~50 ms)
void cleaningUpdate(float pressureBar);

// Pump dimmer level the cleaning cycle wants right now (255 when inactive)
uint8_t cleaningPumpLevel();

// Physical brew button was pressed during cleaning: the user has toggled the
// machine themselves, so abort WITHOUT pulsing the button again
void cleaningAbortFromButton();

// Status accessors for /state and the dashboard
const char* cleaningPhaseName();
const char* cleaningStateName();
int cleaningCurrentCycle();       // 1-based flush number within the phase
float cleaningStateElapsedS();
float cleaningLastFillPeakBar();  // Peak of the previous fill; 0 until one ran
bool cleaningLastFillReachedMax();// False = pressure never built (no blind basket?)

#endif // CLEANING_CYCLE_H
