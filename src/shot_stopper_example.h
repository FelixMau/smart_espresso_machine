#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <rbdimmerESP32.h>

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

#define DEBUG false

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

#define PRESSURE_PIN 33 // Analog pin for pressure sensor (MPX5500 or similar)

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

  // Add these:
  float goalWeight;
  float weightOffset;
};

//Initialize shot
Shot shot = {
  0,0,0,0,{},{},0,false,ENDTYPE::UNDEF,
  0, // pressure
  // Example: 3 pressure goals by time
  {
    {0.0f, 2.0f},    // 0s: 2 bar
    {5.0f, 9.0f},    // 5s: 9 bar
    {20.0f, 6.0f}    // 20s: 6 bar
  },
  3, // numPressureGoalsByTime
  // Example: 1 pressure goal by time left
  {
    {5.0f, 4.0f}     // 5s left: 4 bar
  },
  1  // numPressureGoalsByTimeLeft
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
  Serial.print("Pressure: ");
  Serial.print(s->pressure);
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
    Serial.println("shot started");
    shot.start_timestamp_s = seconds_f();
    shot.shotTimer = 0;
    shot.datapoints = 0;
    scale.resetTimer();
    scale.startTimer();
    if(AUTOTARE){
      scale.tare();
    }
    Serial.println("Weight Timer End");
  }else{
    Serial.print("ShotEnded by ");
    switch (shot.end) {
      case ENDTYPE::TIME:
      Serial.println("time");
      break;
    case ENDTYPE::WEIGHT:
    Serial.println("weight");
      break;
    case ENDTYPE::BUTTON:
    Serial.println("button");
      break;
    case ENDTYPE::UNDEF:
      Serial.println("undef");
      break;
    }

    shot.end_s = seconds_f() - shot.start_timestamp_s;
    scale.stopTimer();
    if(MOMENTARY &&
      (ENDTYPE::WEIGHT == shot.end || ENDTYPE::TIME == shot.end)){
      //Pulse button to stop brewing
      digitalWrite(OUT,HIGH);Serial.println("wrote high");
      delay(1000);
      digitalWrite(OUT,LOW);Serial.println("wrote low");
    }else if(!MOMENTARY){
      buttonLatched = false;
      buttonPressed = false;
      Serial.println("Button Unlatched and not pressed");
      digitalWrite(OUT,LOW); Serial.println("wrote low");
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

    Serial.print(" ");
    Serial.print(shot->shotTimer);

    // Get the likely end time of the shot
    calculateEndTime(shot);
    Serial.print(" ");
    Serial.print(shot->expected_end_s);

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

    // Pressure control logic
    int setLevel = 0;
    if (shot->pressure < goalPressure) {
      digitalWrite(DIMMER_PIN, HIGH); // Ensure pump is ON
      Serial.print(" | Pump ON");
    } else {
      digitalWrite(DIMMER_PIN, LOW); // Ensure pump is OFF
      Serial.print(" | Pump OFF");
    }

    Serial.print(" | GoalPressure: ");
    Serial.print(goalPressure);

    // --- end pressure goal logic ---
    Serial.print(" | CurrentPressure: ");
    Serial.print(shot->pressure);
  }
  Serial.println();
}

void handleMaxDurationReached(Shot* shot) {
  if (shot->brewing && shot->shotTimer > MAX_SHOT_DURATION_S) {
    shot->brewing = false;
    Serial.println("Max brew duration reached");
    shot->end = ENDTYPE::TIME;
    setBrewingState(shot->brewing);
  }
}

void handleShotEnd(Shot* shot, float currentWeight) {
  if (shot->brewing 
      && shot->shotTimer >= shot->expected_end_s 
      && shot->shotTimer > MIN_SHOT_DURATION_S) {
    Serial.println("weight achieved");
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

    Serial.print("I detected a final weight of ");
    Serial.print(currentWeight);
    Serial.print("g. The goal was ");
    Serial.print(shot->goalWeight);
    Serial.print("g with a negative offset of ");
    Serial.print(shot->weightOffset);

    if (abs(currentWeight - shot->goalWeight + shot->weightOffset) > MAX_OFFSET) {
      Serial.print("g. Error assumed. Offset unchanged. ");
    } else {
      Serial.print("g. Next time I'll create an offset of ");
      shot->weightOffset += currentWeight - shot->goalWeight;
      Serial.print(shot->weightOffset);

      EEPROM.write(OFFSET_ADDR, shot->weightOffset * 10); // 1 byte, 0-255
      EEPROM.commit();
    }
    Serial.println();
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
        Serial.println("ButtonPressed");
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
        Serial.println("Button Released");
        buttonState = RELEASED;
        buttonPressed = false;
        shot.brewing = !shot.brewing;
        if (!shot.brewing) {
          shot.end = ENDTYPE::BUTTON;
        }
        setBrewingState(shot.brewing);
      } else if (!MOMENTARY && shot.brewing && !buttonLatched && (shot.shotTimer > MIN_SHOT_DURATION_S)) {
        Serial.println("Button Latched");
        buttonState = HELD;
        buttonLatched = true;
        digitalWrite(OUT, HIGH);
        Serial.println("wrote high");
        if (AUTOTARE) {
          scale.tare();
        }
      }
      break;

    case HELD:
      if (newButtonState) {
        Serial.println("Button Released");
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

