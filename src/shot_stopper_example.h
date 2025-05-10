#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

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
  #define OUT         35
  #define REED_IN     25
#else //todo: find nano esp32 identifier
  //LED's are defined by framework
  #define IN          34
  #define OUT         35
  #define REED_IN     25
#endif 

#define BUTTON_STATE_ARRAY_LENGTH 8

typedef enum {BUTTON, WEIGHT, TIME, UNDEF} ENDTYPE;

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
  float start_timestamp_s; // Relative to runtime
  float shotTimer;         // Reset when the final drip measurement is made
  float end_s;             // Number of seconds after the shot started
  float expected_end_s;    // Estimated duration of the shot
  float weight[1000];      // A scatter plot of the weight measurements, along with time_s[]
  float time_s[1000];      // Number of seconds after the shot starte
  int datapoints;          // Number of datapoitns in the scatter plot
  bool brewing;            // True when actively brewing, otherwise false
  ENDTYPE end;
};

//Initialize shot
Shot shot = {0,0,0,0,{},{},0,false,ENDTYPE::UNDEF};

//BLE peripheral device
BLEService weightService("0x0FFE"); // create service
BLEByteCharacteristic weightCharacteristic("0xFF11",  BLEWrite | BLERead);

float seconds_f() {
  return millis() / 1000.0;
}

void calculateEndTime(Shot* s, float goalWeight, float weightOffset) {
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
    s->expected_end_s = (goalWeight - weightOffset - b) / m;
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
      delay(300);
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

void updateShotTrajectory(Shot* shot, float currentWeight, float goalWeight, float weightOffset) {
  if (shot->brewing) {
    shot->time_s[shot->datapoints] = seconds_f() - shot->start_timestamp_s;
    shot->weight[shot->datapoints] = currentWeight;
    shot->shotTimer = shot->time_s[shot->datapoints];
    shot->datapoints++;

    Serial.print(" ");
    Serial.print(shot->shotTimer);

    // Get the likely end time of the shot
    calculateEndTime(shot, goalWeight, weightOffset);
    Serial.print(" ");
    Serial.print(shot->expected_end_s);
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

void detectShotError(Shot* shot, float currentWeight, float goalWeight, float* weightOffset) {
  if (shot->start_timestamp_s
      && shot->end_s
      && currentWeight >= (goalWeight - *weightOffset)
      && seconds_f() > shot->start_timestamp_s + shot->end_s + DRIP_DELAY_S) {
    shot->start_timestamp_s = 0;
    shot->end_s = 0;

    Serial.print("I detected a final weight of ");
    Serial.print(currentWeight);
    Serial.print("g. The goal was ");
    Serial.print(goalWeight);
    Serial.print("g with a negative offset of ");
    Serial.print(*weightOffset);

    if (abs(currentWeight - goalWeight + *weightOffset) > MAX_OFFSET) {
      Serial.print("g. Error assumed. Offset unchanged. ");
    } else {
      Serial.print("g. Next time I'll create an offset of ");
      *weightOffset += currentWeight - goalWeight;
      Serial.print(*weightOffset);

      EEPROM.write(OFFSET_ADDR, *weightOffset * 10); // 1 byte, 0-255
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
