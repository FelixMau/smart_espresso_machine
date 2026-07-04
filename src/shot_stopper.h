#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "debug.h"
#include "shot_history.h"

#define MAX_OFFSET 5                // In case an error in brewing occured
#define MIN_SHOT_DURATION_S 3       //Useful for flushing the group.
                                    // This ensure that the system will ignore
                                    // "shots" that last less than this duration
#define MAX_SHOT_DURATION_S 50      //Primarily useful for latching switches, since user
                                    // looses control of the paddle once the system
                                    // latches.
#define BUTTON_READ_PERIOD_MS 5
#define DRIP_DELAY_S          3     // Time after the shot ended to measure the final weight

#define EEPROM_SIZE 2  // This is 1-Byte
#define WEIGHT_ADDR 0  // Use the first byte of EEPROM to store the goal weight
#define OFFSET_ADDR 1  

#define datapoints_trend_line 10                        // Number of datapoints used to calculate trend line

//User defined***
#define MOMENTARY true        //Define brew switch style. 
                              // True for momentary switches such as GS3 AV, Silvia Pro
                              // false for latching switches such as Linea Mini/Micra
#define REEDSWITCH false      // Set to true if the brew state is being determined 
                              //  by a reed switch attached to the brew solenoid
#define AUTOTARE true         // Automatically tare when shot is started 
                              //  and 3 seconds after a latching switch brew 
                              // (as defined by MOMENTARY)

#define DEBUGMODE_ACAIA false
//***************

// Board Hardware 
#define BUTTON_READ_PIN          34
#define OUT         22
#define REED_IN     25


#define PRESSURE_PIN 32 // Analog pin for pressure sensor (MPX5500 or similar)

#define BUTTON_STATE_ARRAY_LENGTH 8
extern const int DIMMER_PIN;

// WEB = stopped from the dashboard (presses the machine button like WEIGHT/TIME)
// BUTTON = ended without machine action (physical button or dashboard reset)
typedef enum {BUTTON, WEIGHT, TIME, WEB, UNDEF} ENDTYPE;

// Helper struct for pressure goal pairs
struct PressureGoalByTime {
  float time_s;      // seconds from shot start
  float pressure;    // goal pressure at this time
};

struct PressureGoalByTimeLeft {
  float time_left_s; // seconds left until expected end
  float pressure;    // override goal pressure from this time left onwards
};

#define MAX_PRESSURE_GOALS 8

AcaiaArduinoBLE scale(DEBUGMODE_ACAIA);

// Scale state shared between the scale task (owner of ALL BLE/scale calls,
// see main.cpp:scaleTask) and the control task (consumer). The control task
// never touches the scale directly, so a disconnected scale can never block
// brewing logic or starve the web server. Commands to the scale are queued
// as flags, mirroring the webStartRequest pattern in webserver.h.
volatile bool scaleConnected = false;
volatile bool scaleNewWeight = false;            // set by scale task per weight packet
volatile bool scaleStartSequenceRequest = false; // resetTimer + startTimer (+ tare)
volatile bool scaleStopTimerRequest = false;
volatile bool scaleTareRequest = false;

float error = 0;
int buttonArr[BUTTON_STATE_ARRAY_LENGTH];            // last 4 readings of the button

// button 
int button_read = REEDSWITCH ? REED_IN : BUTTON_READ_PIN;
bool buttonPressed = false; //physical status of button
bool buttonLatched = false; //electrical status of button
unsigned long lastButtonRead_ms = 0;
int newButtonState = 0;

enum ButtonState { IDLE, PRESSED, HELD, RELEASED };
ButtonState buttonState = IDLE;

struct Shot {
  float start_timestamp_s;
  float shot_timer;
  float end_s;
  float expected_end_s;
  float weight[1000];
  float time_s[1000];
  int datapoints;
  bool brewing;
  ENDTYPE end;
  float pressure;
  // Pressure goals...
  PressureGoalByTime pressure_goal_by_time[MAX_PRESSURE_GOALS];
  int num_pressure_goals_by_time;
  PressureGoalByTimeLeft pressure_goal_by_time_left[MAX_PRESSURE_GOALS];
  int num_pressure_goals_by_time_left;
  float current_goal_pressure;

  // Add these:
  float goal_weight;
  float weight_offset;

  // Published for web dashboard monitoring
  int pump_pwm;         // Last PWM value written to the dimmer (0-255)
  float peak_pressure;  // Highest pressure seen during the current shot
};

//Initialize shot
Shot shot = {
  0, // start_timestamp_s
  0, // shotTimer
  0, // end_s
  0, // expected_end_s
  {}, // weight
  {}, // time_s
  0, // datapoints
  false, // brewing
  ENDTYPE::UNDEF, // end
  0, // pressure
  // pressureGoalByTime
  {
    {0.0f, 2.0f},    // 0s: 2 bar
    {5.0f, 9.0f},    // 5s: 9 bar
    {20.0f, 6.0f}    // 20s: 6 bar
  },
  3, // numPressureGoalsByTime
  // pressureGoalByTimeLeft
  {
    {5.0f, 4.0f}     // 5s left: 4 bar
  },
  1, // numPressureGoalsByTimeLeft
  0, // current_goal_pressure
  0, // goalWeight (set from EEPROM later)
  0, // weightOffset (set from EEPROM later)
  255, // pumpPwm (idle = full speed)
  0  // peakPressure
};

// Expose shot for use in other modules
extern Shot shot;

//BLE peripheral device
BLEService weightService("0x0FFE"); // create service
BLEByteCharacteristic weightCharacteristic("0xFF11",  BLEWrite | BLERead);

float seconds_f() {
  return millis() / 1000.0;
}

void updatePressureSensor(Shot* s) {
  int raw = analogRead(PRESSURE_PIN);
  float voltage = raw * (3.3f / 4095.0f);
  if (voltage < 0.4f) {
    s->pressure = 0.0f;
  } else if (voltage > 3.3f) {
    s->pressure = 16.0f;
  } else {
    s->pressure = (voltage - 0.4f) * (16.0f / (3.3f - 0.4f));
  }
  DEBUG_SENSOR_PRINT("Pressure: %.2f bar", s->pressure);
}


void calculateEndTime(Shot* s) {
  // Do not predict end time if there aren't enough espresso measurements yet
  if ((s->datapoints < datapoints_trend_line) || (s->weight[s->datapoints - 1] < 10)) {
    s->expected_end_s = MAX_SHOT_DURATION_S;
  } else {
    // Get line of best fit (y=mx+b) from the last 10 measurements
    float sumXY = 0, sumX = 0, sumY = 0, sumSquaredX = 0, m = 0, b = 0, meanX = 0, meanY = 0;

    for (int i = s->datapoints - datapoints_trend_line; i < s->datapoints; i++) {
      sumXY += s->time_s[i] * s->weight[i];
      sumX += s->time_s[i];
      sumY += s->weight[i];
      sumSquaredX += (s->time_s[i] * s->time_s[i]);
    }

    m = (datapoints_trend_line * sumXY - sumX * sumY) / (datapoints_trend_line * sumSquaredX - (sumX * sumX));
    meanX = sumX / datapoints_trend_line;
    meanY = sumY / datapoints_trend_line;
    b = meanY - m * meanX;

    // Calculate time at which goal weight will be reached (x = (y-b)/m)
    s->expected_end_s = (s->goal_weight - s->weight_offset - b) / m;
  }
}

void setBrewingState(bool brewing) {
  if(brewing){
    DEBUG_SHOT_PRINT("Shot started");
    shot.start_timestamp_s = seconds_f();
    shot.shot_timer = 0;
    shot.datapoints = 0;
    shot.peak_pressure = 0;
    scaleStartSequenceRequest = true; // scale task: resetTimer + startTimer (+ tare)
  }else{
    const char* endReason = "UNDEF";
    switch (shot.end) {
      case ENDTYPE::TIME:
        endReason = "TIME";
        break;
      case ENDTYPE::WEIGHT:
        endReason = "WEIGHT";
        break;
      case ENDTYPE::BUTTON:
        endReason = "BUTTON";
        break;
      case ENDTYPE::WEB:
        endReason = "WEB";
        break;
      case ENDTYPE::UNDEF:
        endReason = "UNDEF";
        break;
    }
    DEBUG_SHOT_PRINT("Shot ended by: %s (duration: %.1f s)", endReason, seconds_f() - shot.start_timestamp_s);

    shot.end_s = seconds_f() - shot.start_timestamp_s;

    // Snapshot the trajectory into the history ring buffer before the next
    // shot overwrites it. Skip flushes shorter than MIN_SHOT_DURATION_S.
    if (shot.end_s >= MIN_SHOT_DURATION_S) {
      recordShot(shot.time_s, shot.weight, shot.datapoints,
                 shot.end_s, shot.peak_pressure, (int)shot.end);
    }

    scaleStopTimerRequest = true;
    if(MOMENTARY &&
      (ENDTYPE::WEIGHT == shot.end || ENDTYPE::TIME == shot.end || ENDTYPE::WEB == shot.end)){
      //Pulse button to stop brewing
      DEBUG_SHOT_PRINT("Writing solenoid HIGH");
      digitalWrite(OUT,HIGH);
      delay(1000);
      DEBUG_SHOT_PRINT("Writing solenoid LOW");
      digitalWrite(OUT,LOW);
    }else if(!MOMENTARY){
      buttonLatched = false;
      buttonPressed = false;
      DEBUG_SHOT_PRINT("Button unlatched and not pressed");
      digitalWrite(OUT,LOW);
    }
  } 

  // Reset
  shot.end = ENDTYPE::UNDEF;
}

void updateShotTrajectory(Shot* shot, float currentWeight) {
  if (shot->brewing) {
    updatePressureSensor(shot);
    if (shot->pressure > shot->peak_pressure) {
      shot->peak_pressure = shot->pressure;
    }
    shot->time_s[shot->datapoints] = seconds_f() - shot->start_timestamp_s;
    shot->weight[shot->datapoints] = currentWeight;
    shot->shot_timer = shot->time_s[shot->datapoints];
    shot->datapoints++;

    // Get the likely end time of the shot
    calculateEndTime(shot);

    // --- Pressure goal logic ---
    float goalPressure = 0.0f;
    // 1. Find the latest pressure goal by time (<= current time)
    for (int i = 0; i < shot->num_pressure_goals_by_time; ++i) {
      if (shot->shot_timer >= shot->pressure_goal_by_time[i].time_s) {
        goalPressure = shot->pressure_goal_by_time[i].pressure;
      }
    }
    // 2. Check for override by time left
    float timeLeft = shot->expected_end_s - shot->shot_timer;
    for (int i = 0; i < shot->num_pressure_goals_by_time_left; ++i) {
      if (timeLeft <= shot->pressure_goal_by_time_left[i].time_left_s) {
        goalPressure = shot->pressure_goal_by_time_left[i].pressure;
      }
    }
    shot->current_goal_pressure = goalPressure;

    // Detailed shot trajectory logging
    DEBUG_SHOT_PRINT("Time: %.1f s | Weight: %.1f g | Expected end: %.1f s | Goal weight: %.0f g | Pump: %s | Goal pressure: %.1f bar | Current pressure: %.1f bar",
      shot->shot_timer,
      shot->weight[shot->datapoints - 1],
      shot->expected_end_s,
      shot->goal_weight,
      (shot->pressure < goalPressure) ? "ON" : "OFF",
      goalPressure,
      shot->pressure
    );
  }
}

void handleMaxDurationReached(Shot* shot) {
  if (shot->brewing && shot->shot_timer > MAX_SHOT_DURATION_S) {
    shot->brewing = false;
    DEBUG_SHOT_PRINT("Max brew duration reached (%.1f s > %.1f s)", shot->shot_timer, MAX_SHOT_DURATION_S);
    shot->end = ENDTYPE::TIME;
    setBrewingState(shot->brewing);
  }
}

void handleShotEnd(Shot* shot, float currentWeight) {
  if (shot->brewing 
      && shot->shot_timer >= shot->expected_end_s 
      && shot->shot_timer > MIN_SHOT_DURATION_S) {
    DEBUG_SHOT_PRINT("Goal weight achieved (%.1f g >= %.1f g - %.1f g offset)", currentWeight, shot->goal_weight, shot->weight_offset);
    shot->brewing = false;
    shot->end = ENDTYPE::WEIGHT;
    setBrewingState(shot->brewing);
  }
}

void detectShotError(Shot* shot, float currentWeight) {
  if (shot->start_timestamp_s
      && shot->end_s
      && currentWeight >= (shot->goal_weight - shot->weight_offset)
      && seconds_f() > shot->start_timestamp_s + shot->end_s + DRIP_DELAY_S) {
    shot->start_timestamp_s = 0;
    shot->end_s = 0;

    if (abs(currentWeight - shot->goal_weight + shot->weight_offset) > MAX_OFFSET) {
      DEBUG_SHOT_PRINT("Weight error detected: final=%.1f g, goal=%.0f g, offset=%.1f g - Error too large, offset unchanged",
        currentWeight, shot->goal_weight, shot->weight_offset);
    } else {
      float newOffset = shot->weight_offset + currentWeight - shot->goal_weight;
      DEBUG_SHOT_PRINT("Weight correction: final=%.1f g, goal=%.0f g, old_offset=%.1f g → new_offset=%.1f g",
        currentWeight, shot->goal_weight, shot->weight_offset, newOffset);
      shot->weight_offset = newOffset;

      EEPROM.write(OFFSET_ADDR, (uint8_t)(shot->weight_offset * 10)); // 1 byte, 0-255
      EEPROM.commit();
      DEBUG_SHOT_PRINT("New offset saved to EEPROM");
    }
  }
}

void handleButtonLogic() {
  // Read button every period
  if (millis() > (lastButtonRead_ms + BUTTON_READ_PERIOD_MS)) {
    lastButtonRead_ms = millis();

    // Shift button state array
    for (int i = BUTTON_STATE_ARRAY_LENGTH - 2; i >= 0; i--) {
      buttonArr[i + 1] = buttonArr[i];
    }
    buttonArr[0] = digitalRead(button_read); // Active Low
        // Determine new button state
    newButtonState = 0;
    for (int i = 0; i < BUTTON_STATE_ARRAY_LENGTH; i++) {
      if (buttonArr[i]) {
        newButtonState = 1;
      }
    }

    // Handle reed switch noise
    if (REEDSWITCH && !shot.brewing && seconds_f() < (shot.start_timestamp_s + shot.end_s + 0.5)) {
      newButtonState = 0;
    }
  }

  // Update button state machine
  switch (buttonState) {
    case IDLE:
      if (newButtonState) {
        DEBUG_BUTTON_PRINT("Button pressed");
        buttonState = PRESSED;
        buttonPressed = true;
        if (!MOMENTARY) {
          shot.brewing = true;
          setBrewingState(shot.brewing);
        }
      }
      break;

    case PRESSED:
      if (!newButtonState) {
        DEBUG_BUTTON_PRINT("Button released");
        buttonState = RELEASED;
        buttonPressed = false;
        shot.brewing = !shot.brewing;
        if (!shot.brewing) {
          shot.end = ENDTYPE::BUTTON;
        }
        setBrewingState(shot.brewing);
      } else if (!MOMENTARY && shot.brewing && !buttonLatched && (shot.shot_timer > MIN_SHOT_DURATION_S)) {
        DEBUG_BUTTON_PRINT("Button latched");
        buttonState = HELD;
        buttonLatched = true;
        DEBUG_BUTTON_PRINT("Writing solenoid HIGH");
        digitalWrite(OUT, HIGH);
        if (AUTOTARE) {
          scaleTareRequest = true;
        }
      }
      break;

    case HELD:
      if (newButtonState) {
        DEBUG_BUTTON_PRINT("Button released");
        buttonState = RELEASED;
        buttonPressed = false;
        shot.brewing = false;
        shot.end = ENDTYPE::BUTTON;
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

