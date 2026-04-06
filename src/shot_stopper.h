#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "debug.h"

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

#define N 10                        // Number of datapoints used to calculate trend line

//User defined***
#define MOMENTARY true        //Define brew switch style. 
                              // True for momentary switches such as GS3 AV, Silvia Pro
                              // false for latching switches such as Linea Mini/Micra
#define REEDSWITCH false      // Set to true if the brew state is being determined 
                              //  by a reed switch attached to the brew solenoid
#define AUTOTARE true         // Automatically tare when shot is started 
                              //  and 3 seconds after a latching switch brew 
                              // (as defined by MOMENTARY)

#define DEBUG false
//***************

// Board Hardware 
#ifdef ARDUINO_ESP32S3_DEV
  #define IN          34
  #define OUT         22
  #define REED_IN     25
#else //todo: find nano esp32 identifier
  //LED's are defined by framework
  #define IN          13
  #define OUT         22
  #define REED_IN     25
#endif 

#define PRESSURE_PIN 32 // Analog pin for pressure sensor (MPX5500 or similar)

#define BUTTON_STATE_ARRAY_LENGTH 8
extern const int DIMMER_PIN;

typedef enum {BUTTON, WEIGHT, TIME, UNDEF} ENDTYPE;

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

AcaiaArduinoBLE scale(DEBUG);
float error = 0;
int buttonArr[BUTTON_STATE_ARRAY_LENGTH];            // last 4 readings of the button

// button 
int in = REEDSWITCH ? REED_IN : IN;
bool buttonPressed = false; //physical status of button
bool buttonLatched = false; //electrical status of button
unsigned long lastButtonRead_ms = 0;
int newButtonState = 0;

enum ButtonState { IDLE, PRESSED, HELD, RELEASED };
ButtonState buttonState = IDLE;

struct Shot {
  float start_timestamp_s;
  float shotTimer;
  float end_s;
  float expected_end_s;
  float weight[1000];
  float time_s[1000];
  int datapoints;
  bool brewing;
  ENDTYPE end;
  float pressure;
  // Pressure goals...
  PressureGoalByTime pressureGoalByTime[MAX_PRESSURE_GOALS];
  int numPressureGoalsByTime;
  PressureGoalByTimeLeft pressureGoalByTimeLeft[MAX_PRESSURE_GOALS];
  int numPressureGoalsByTimeLeft;
  float current_goal_pressure;

  // Add these:
  float goalWeight;
  float weightOffset;
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
  0  // weightOffset (set from EEPROM later)
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
  if ((s->datapoints < N) || (s->weight[s->datapoints - 1] < 10)) {
    s->expected_end_s = MAX_SHOT_DURATION_S;
  } else {
    // Get line of best fit (y=mx+b) from the last 10 measurements
    float sumXY = 0, sumX = 0, sumY = 0, sumSquaredX = 0, m = 0, b = 0, meanX = 0, meanY = 0;

    for (int i = s->datapoints - N; i < s->datapoints; i++) {
      sumXY += s->time_s[i] * s->weight[i];
      sumX += s->time_s[i];
      sumY += s->weight[i];
      sumSquaredX += (s->time_s[i] * s->time_s[i]);
    }

    m = (N * sumXY - sumX * sumY) / (N * sumSquaredX - (sumX * sumX));
    meanX = sumX / N;
    meanY = sumY / N;
    b = meanY - m * meanX;

    // Calculate time at which goal weight will be reached (x = (y-b)/m)
    s->expected_end_s = (s->goalWeight - s->weightOffset - b) / m;
  }
}

void setBrewingState(bool brewing) {
  if(brewing){
    DEBUG_SHOT_PRINT("Shot started");
    shot.start_timestamp_s = seconds_f();
    shot.shotTimer = 0;
    shot.datapoints = 0;
    scale.resetTimer();
    delay(50); // Small delay to allow scale to process reset command
    scale.startTimer();
    delay(50); // Small delay to allow scale to process start command
    if(AUTOTARE){
      scale.tare();
    }
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
      case ENDTYPE::UNDEF:
        endReason = "UNDEF";
        break;
    }
    DEBUG_SHOT_PRINT("Shot ended by: %s (duration: %.1f s)", endReason, seconds_f() - shot.start_timestamp_s);

    shot.end_s = seconds_f() - shot.start_timestamp_s;
    scale.stopTimer();
    if(MOMENTARY &&
      (ENDTYPE::WEIGHT == shot.end || ENDTYPE::TIME == shot.end)){
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
    shot->time_s[shot->datapoints] = seconds_f() - shot->start_timestamp_s;
    shot->weight[shot->datapoints] = currentWeight;
    shot->shotTimer = shot->time_s[shot->datapoints];
    shot->datapoints++;

    // Get the likely end time of the shot
    calculateEndTime(shot);

    // --- Pressure goal logic ---
    float goalPressure = 0.0f;
    // 1. Find the latest pressure goal by time (<= current time)
    for (int i = 0; i < shot->numPressureGoalsByTime; ++i) {
      if (shot->shotTimer >= shot->pressureGoalByTime[i].time_s) {
        goalPressure = shot->pressureGoalByTime[i].pressure;
      }
    }
    // 2. Check for override by time left
    float timeLeft = shot->expected_end_s - shot->shotTimer;
    for (int i = 0; i < shot->numPressureGoalsByTimeLeft; ++i) {
      if (timeLeft <= shot->pressureGoalByTimeLeft[i].time_left_s) {
        goalPressure = shot->pressureGoalByTimeLeft[i].pressure;
      }
    }
    shot->current_goal_pressure = goalPressure;

    // Detailed shot trajectory logging
    DEBUG_SHOT_PRINT("Time: %.1f s | Weight: %.1f g | Expected end: %.1f s | Goal weight: %.0f g | Pump: %s | Goal pressure: %.1f bar | Current pressure: %.1f bar",
      shot->shotTimer,
      shot->weight[shot->datapoints - 1],
      shot->expected_end_s,
      shot->goalWeight,
      (shot->pressure < goalPressure) ? "ON" : "OFF",
      goalPressure,
      shot->pressure
    );
  }
}

void handleMaxDurationReached(Shot* shot) {
  if (shot->brewing && shot->shotTimer > MAX_SHOT_DURATION_S) {
    shot->brewing = false;
    DEBUG_SHOT_PRINT("Max brew duration reached (%.1f s > %.1f s)", shot->shotTimer, MAX_SHOT_DURATION_S);
    shot->end = ENDTYPE::TIME;
    setBrewingState(shot->brewing);
  }
}

void handleShotEnd(Shot* shot, float currentWeight) {
  if (shot->brewing 
      && shot->shotTimer >= shot->expected_end_s 
      && shot->shotTimer > MIN_SHOT_DURATION_S) {
    DEBUG_SHOT_PRINT("Goal weight achieved (%.1f g >= %.1f g - %.1f g offset)", currentWeight, shot->goalWeight, shot->weightOffset);
    shot->brewing = false;
    shot->end = ENDTYPE::WEIGHT;
    setBrewingState(shot->brewing);
  }
}

void detectShotError(Shot* shot, float currentWeight) {
  if (shot->start_timestamp_s
      && shot->end_s
      && currentWeight >= (shot->goalWeight - shot->weightOffset)
      && seconds_f() > shot->start_timestamp_s + shot->end_s + DRIP_DELAY_S) {
    shot->start_timestamp_s = 0;
    shot->end_s = 0;

    if (abs(currentWeight - shot->goalWeight + shot->weightOffset) > MAX_OFFSET) {
      DEBUG_SHOT_PRINT("Weight error detected: final=%.1f g, goal=%.0f g, offset=%.1f g - Error too large, offset unchanged",
        currentWeight, shot->goalWeight, shot->weightOffset);
    } else {
      float newOffset = shot->weightOffset + currentWeight - shot->goalWeight;
      DEBUG_SHOT_PRINT("Weight correction: final=%.1f g, goal=%.0f g, old_offset=%.1f g → new_offset=%.1f g",
        currentWeight, shot->goalWeight, shot->weightOffset, newOffset);
      shot->weightOffset = newOffset;

      EEPROM.write(OFFSET_ADDR, (uint8_t)(shot->weightOffset * 10)); // 1 byte, 0-255
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
    buttonArr[0] = digitalRead(in); // Active Low
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
      } else if (!MOMENTARY && shot.brewing && !buttonLatched && (shot.shotTimer > MIN_SHOT_DURATION_S)) {
        DEBUG_BUTTON_PRINT("Button latched");
        buttonState = HELD;
        buttonLatched = true;
        DEBUG_BUTTON_PRINT("Writing solenoid HIGH");
        digitalWrite(OUT, HIGH);
        if (AUTOTARE) {
          scale.tare();
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

