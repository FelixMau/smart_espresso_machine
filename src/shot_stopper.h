#ifndef SHOT_STOPPER_H
#define SHOT_STOPPER_H

// ============================================================================
// SHOT STOPPER - CORE BREWING LOGIC
// ============================================================================
// Shot state, button state machine, end-time regression and EEPROM learning.
// Implementations live in shot_stopper.cpp.

#include <Arduino.h>
#include <AcaiaArduinoBLE.h>

// ============================================================================
// BREWING PARAMETERS
// ============================================================================

#define MAX_OFFSET 5           // Largest weight error (g) accepted for offset learning
#define MIN_SHOT_DURATION_S 3  // Ignore "shots" shorter than this (group flushes)
#define MAX_SHOT_DURATION_S 50 // Safety timeout; vital for latching switches, since
                               //  the user loses the paddle once the system latches
#define BUTTON_READ_PERIOD_MS 5
#define DRIP_DELAY_S 3         // Wait after shot end before measuring final weight

#define EEPROM_SIZE 2  // Two 1-byte slots
#define WEIGHT_ADDR 0  // EEPROM byte 0: goal weight (g)
#define OFFSET_ADDR 1  // EEPROM byte 1: weight offset x 10

#define TREND_LINE_DATAPOINTS 10  // Regression window (accuracy vs latency)
#define MAX_SHOT_DATAPOINTS 1000  // Capacity of the per-shot trajectory arrays

// ============================================================================
// USER CONFIGURATION
// ============================================================================

#define MOMENTARY true    // Brew switch style: true for momentary switches such as
                          //  GS3 AV, Silvia Pro; false for latching switches such
                          //  as Linea Mini/Micra
#define REEDSWITCH false  // True if the brew state is determined by a reed switch
                          //  attached to the brew solenoid
#define AUTOTARE true     // Automatically tare when a shot is started and 3 seconds
                          //  after a latching switch brew (as defined by MOMENTARY)

#define DEBUGMODE_ACAIA false

// ============================================================================
// HARDWARE PINS
// ============================================================================

// Button input pin is board-dependent. On the classic ESP32 (upesy_wroom) the
// brew button (schematic net "IN") is wired to GPIO13, which supports the
// internal pull-up that pinMode(..., INPUT_PULLUP) relies on. GPIO34 is
// input-only and has NO internal pull-up on the classic ESP32, so reading the
// button there leaves the pin floating and presses go undetected.
#ifdef ARDUINO_ESP32S3_DEV
  #define BUTTON_READ_PIN 34
#else
  #define BUTTON_READ_PIN 13
#endif
#define PRESS_BUTTON_PIN 22  // Button output (opto-coupled to the machine)
#define REED_IN 25

#define PRESSURE_PIN 32  // Analog pin for pressure sensor (MPX5500 or similar)

// EMA smoothing factor for the pressure reading, sampled at the ~10 ms
// control cadence (time constant ~0.1 s). Raw single ADC reads are noisy
// and would jitter the control law and its dP/dt term.
#define PRESSURE_FILTER_ALPHA 0.1f

// Active button input: reed switch or brew button, depending on REEDSWITCH
extern const int BUTTON_INPUT_PIN;

// ============================================================================
// TYPES
// ============================================================================

// WEB = stopped from the dashboard (presses the machine button like WEIGHT/TIME)
// BUTTON = ended without machine action (physical button or dashboard reset)
enum class EndType { BUTTON, WEIGHT, TIME, WEB, UNDEF };

struct PressureGoalByTime {
  float timeS;     // Seconds from shot start
  float pressure;  // Goal pressure at this time
};

struct PressureGoalByTimeLeft {
  float timeLeftS; // Seconds left until expected end
  float pressure;  // Override goal pressure from this time left onwards
};

#define MAX_PRESSURE_GOALS 8

struct Shot {
  float startTimestampS;               // Boot-relative start time
  float shotTimer;                     // Seconds since shot start
  float endS;                          // Duration of the finished shot
  float expectedEndS;                  // Regression-predicted end time
  float weight[MAX_SHOT_DATAPOINTS];   // Weight trajectory
  float timeS[MAX_SHOT_DATAPOINTS];    // Time axis of the trajectory
  float pressureTrace[MAX_SHOT_DATAPOINTS]; // Per-datapoint pressure, kept for
                                       //  shot history / Beanconqueror export
  int datapoints;                      // Valid points in the arrays above
  bool brewing;
  EndType end;
  float pressure;                      // Latest pressure reading (bar)

  // Pressure goals
  PressureGoalByTime pressureGoalByTime[MAX_PRESSURE_GOALS];
  int numPressureGoalsByTime;
  PressureGoalByTimeLeft pressureGoalByTimeLeft[MAX_PRESSURE_GOALS];
  int numPressureGoalsByTimeLeft;
  float currentGoalPressure;

  // User parameters (persisted to EEPROM)
  float goalWeight;
  float weightOffset;

  // Published for web dashboard monitoring
  int pumpPwm;         // Last PWM value written to the dimmer (0-255)
  float peakPressure;  // Highest pressure seen during the current shot
  float pumpFlow;      // Model-estimated pump flow (ml/s, pump_model.cpp)
};

// ============================================================================
// SHARED STATE
// ============================================================================

// Global shot state, shared across modules
extern Shot shot;

// BLE scale, owned exclusively by the scale task (main.cpp:scaleTask)
extern AcaiaArduinoBLE scale;

// Scale state shared between the scale task (owner of ALL BLE/scale calls)
// and the control task (consumer). The control task never touches the scale
// directly, so a disconnected scale can never block brewing logic or starve
// the web server. Commands to the scale are queued as flags, mirroring the
// webStartRequest pattern in webserver.h.
extern volatile bool scaleConnected;
extern volatile bool scaleNewWeight;            // Set by scale task per weight packet
extern volatile bool scaleStartSequenceRequest; // resetTimer + startTimer (+ tare)
extern volatile bool scaleStopTimerRequest;
extern volatile bool scaleTareRequest;
extern volatile float currentWeight;            // Live scale reading (g)

// Electrical status of the button output (latching machines)
extern bool buttonLatched;

// ============================================================================
// FUNCTIONS
// ============================================================================

// Seconds since boot as float
float secondsSinceBoot();

// Human-readable name of an EndType value
const char* endReasonName(EndType end);

// Convert MPX5500 sensor voltage to pressure (bar), clamped to 0-16
float pressureBarFromVoltage(float voltage);

// Read the pressure sensor and store the result in s->pressure
void updatePressureSensor(Shot* s);

// Start or end a shot: timers, scale commands, machine button, history record
void setBrewingState(bool brewing);

// Append a weight datapoint, update prediction and current pressure goal
void updateShotTrajectory(Shot* s, float weight);

// Safety timeout: end the shot after MAX_SHOT_DURATION_S
void handleMaxDurationReached(Shot* s);

// End the shot when the predicted end time is reached
void handleShotEnd(Shot* s, float weight);

// Post-shot offset learning: after DRIP_DELAY_S, adopt small weight errors
// into the offset and persist to EEPROM; larger errors are rejected
void detectShotError(Shot* s, float weight);

// Debounced button state machine (momentary and latching switches)
void handleButtonLogic();

#endif // SHOT_STOPPER_H
