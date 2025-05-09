/*
  shotStopper.ino - Example of using an acaia scale to brew by weight with an espresso machine

  Immediately Connects to a nearby acaia scale, 
  tare's the scale when the "in" gpio is triggered (active low),
  and then triggers the "out" gpio to stop the shot once ( goalWeight - weightOffset ) is achieved.

  Tested on a Acaia Pyxis, Arduino nano ESP32, and La Marzocco GS3. 

  Note that only the EEPROM library only supports ESP32-based controllers.

  To set the Weight over BLE, use a BLE app such as LightBlue to connect
  to the "shotStopper" BLE device and read/write to the weight characteristic,
  otherwise the weight is defaulted to 36g.

  Created by Tate Mazer, 2023.

  Released under the MIT license.

  https://github.com/tatemazer/AcaiaArduinoBLE

*/

#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include "globals.h"

#define MAX_OFFSET 5                // In case an error in brewing occured
#define MIN_SHOT_DURATION_S 3       //Useful for flushing the group.
                                    // This ensure that the system will ignore
                                    // "shots" that last less than this duration
#define MAX_SHOT_DURATION_S 50      //Primarily useful for latching switches, since user
                                    // looses control of the paddle once the system
                                    // latches.
#define BUTTON_READ_PERIOD_MS 5
#define DRIP_DELAY_S 3              // Time after the shot ended to measure the final weight

#define DEBUG false
#define EEPROM_SIZE 2              // Number of bytes used to store weight and offset
#define WEIGHT_ADDR 0            // Address of the weight in EEPROM
#define OFFSET_ADDR 1            // Address of the offset in EEPROM


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
  #define IN          35
  #define OUT         14
  #define REED_IN     32
#else
  #define IN          35
  #define OUT         14
  #define REED_IN     32
#endif 

#define BUTTON_STATE_ARRAY_LENGTH 31

typedef enum {BUTTON, WEIGHT, TIME, UNDEF} ENDTYPE;

// RGB Colors {Red,Green,Blue}
int MAGENTA[3] = {255, 0, 255};
int CYAN[3] = {0, 255, 255};
int YELLOW[3] = {255, 255, 0};
int WHITE[3] = {255, 255, 255};

float error = 0;

// button 
int in = REEDSWITCH ? REED_IN : IN;

#include "globals.h"

// Removed redefinition of Shot
// Use the Shot structure from shotStopper.h

// Updated shot initialization to use Shot::EndType

//BLE peripheral device

// Forward declarations
void setBrewingState(bool brewing);
void calculateEndTime(Shot* currentShot);
float seconds_f();
void setColor(int rgb[3]);

// Removed setup() and loop() functions

// Updated setBrewingState to use Shot::EndType
void setBrewingState(bool brewing) {
  if (brewing) {
    Serial.println("Brewing started.");
    shot.start_timestamp_s = seconds_f();
    shot.shotTimer = 0;
    shot.datapoints = 0;
    scale.resetTimer();
    scale.startTimer();
    if (AUTOTARE) {
      scale.tare();
    }
  } else {
    Serial.println("Brewing stopped.");
    switch (shot.end) {
      case Shot::TIME:
        Serial.println("Shot ended by time.");
        break;
      case Shot::WEIGHT:
        Serial.println("Shot ended by weight.");
        break;
      case Shot::BUTTON:
        Serial.println("Shot ended by button press.");
        break;
      case Shot::UNDEF:
        Serial.println("Shot ended by undefined reason.");
        break;
    }

    shot.end_s = seconds_f() - shot.start_timestamp_s;
    scale.stopTimer();
    if (MOMENTARY &&
        (Shot::WEIGHT == shot.end || Shot::TIME == shot.end)) {
      // Pulse button to stop brewing
      digitalWrite(OUT, HIGH);
      delay(300);
      digitalWrite(OUT, LOW);
    } else if (!MOMENTARY) {
      buttonLatched = false;
      buttonPressed = false;
      digitalWrite(OUT, LOW);
    }
  }

  // Reset
  shot.end = Shot::UNDEF;
}

void calculateEndTime(Shot* currentShot) {
  // Do not predict end time if there aren't enough espresso measurements yet
  if ((currentShot->datapoints < N) || (currentShot->weight[currentShot->datapoints - 1] < 10)) {
    currentShot->expected_end_s = MAX_SHOT_DURATION_S;
  } else {
    // Get line of best fit (y=mx+b) from the last 10 measurements
    float sumXY = 0, sumX = 0, sumY = 0, sumSquaredX = 0, m = 0, b = 0, meanX = 0, meanY = 0;

    for (int i = currentShot->datapoints - N; i < currentShot->datapoints; i++) {
      sumXY += currentShot->time_s[i] * currentShot->weight[i];
      sumX += currentShot->time_s[i];
      sumY += currentShot->weight[i];
      sumSquaredX += (currentShot->time_s[i] * currentShot->time_s[i]);
    }

    m = (N * sumXY - sumX * sumY) / (N * sumSquaredX - (sumX * sumX));
    meanX = sumX / N;
    meanY = sumY / N;
    b = meanY - m * meanX;

    // Calculate time at which goal weight will be reached (x = (y-b)/m)
    currentShot->expected_end_s = (goalWeight - weightOffset - b) / m;
  }
}

float seconds_f(){
  return millis()/1000.0;
}

void setColor(int rgb[3]) {
  currentColor[0] = rgb[0];
  currentColor[1] = rgb[1];
  currentColor[2] = rgb[2];
}


void startBrewing() {
  Serial.println("Brewing started.");
  shot.start_timestamp_s = seconds_f();
  shot.shotTimer = 0;
  shot.datapoints = 0;
  scale.resetTimer();
  scale.startTimer();
  if (AUTOTARE) {
      scale.tare();
  }
  setColor(GREEN);
}

void stopBrewing(Shot::EndType endType) {
  Serial.println("Brewing stopped.");
  shot.end = endType;
  shot.end_s = seconds_f() - shot.start_timestamp_s;
  scale.stopTimer();
  setColor(OFF);
}

void resetShot() {
  Serial.println("Resetting shot.");
  shot = {0, 0, 0, 0, {}, {}, 0, false, Shot::UNDEF};
  setColor(OFF);
}
enum State { IDLE, BREWING, FINISHED, ERROR };
State currentState = IDLE;

void handleState() {
    switch (currentState) {
        case IDLE:
            if (buttonPressed) {
                startBrewing();
                currentState = BREWING;
            }
            break;

        case BREWING:
            if (shot.shotTimer > MAX_SHOT_DURATION_S) {
                stopBrewing(Shot::TIME);
                currentState = FINISHED;
            } else if (currentWeight >= (goalWeight - weightOffset)) {
                stopBrewing(Shot::WEIGHT);
                currentState = FINISHED;
            }
            break;

        case FINISHED:
            if (buttonPressed) {
                resetShot();
                currentState = IDLE;
            }
            break;

        case ERROR:
            // Handle error state (e.g., reset variables, notify user)
            if (buttonPressed) {
                resetShot();
                currentState = IDLE;
            }
            break;
    }
}
