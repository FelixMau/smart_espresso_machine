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

#define SHOT_STOPPER_MAIN

#include <AcaiaArduinoBLE.h>
#include <EEPROM.h>
#include "shared_variables.h"

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
  #define REED_IN     9
#else //todo: find nano esp32 identifier
  //LED's are defined by framework
  #define IN          34
  #define OUT         35
  #define REED_IN     9
#endif 

#define BUTTON_STATE_ARRAY_LENGTH 31

// RGB Colors {Red,Green,Blue}
int RED[3] = {255, 0, 0};
int GREEN[3] = {0, 255, 0};
int BLUE[3] = {0, 0, 255};
int MAGENTA[3] = {255, 0, 255};
int CYAN[3] = {0, 255, 255};
int YELLOW[3] = {255, 255, 0};
int WHITE[3] = {255, 255, 255};
int OFF[3] = {0,0,0};
int currentColor[3] = {0,0,0};

AcaiaArduinoBLE scale(DEBUG);
float currentWeight = 0;
float goalWeight = 0;      // Goal Weight to be read from EEPROM
float weightOffset = 0;
float error = 0;
int buttonArr[BUTTON_STATE_ARRAY_LENGTH];            // last 4 readings of the button

// button 
int in = REEDSWITCH ? REED_IN : IN;
bool buttonPressed = false; //physical status of button
bool buttonLatched = false; //electrical status of button
unsigned long lastButtonRead_ms = 0;
int newButtonState = 0;

//Initialize shot
Shot shot = {0,0,0,0,{},{},0,false,ENDTYPE::UNDEF};

//BLE peripheral device
BLEService weightService("0x0FFE"); // create service
BLEByteCharacteristic weightCharacteristic("0xFF11",  BLEWrite | BLERead);

// Function prototypes
void setBrewingState(bool brewing);
void calculateEndTime(Shot* s);

void setBrewingState(bool brewing) {
  shot.brewing = brewing;
  if (brewing) {
    shot.start_timestamp_s = millis() / 1000.0;
    shot.datapoints = 0;
    scale.tare();
  } else {
    shot.end_s = millis() / 1000.0 - shot.start_timestamp_s;
  }
}

void calculateEndTime(Shot* s) {
  if (s->datapoints < 10 || s->weight[s->datapoints - 1] < 10) {
    s->expected_end_s = 50; // Default max shot duration
    return;
  }
  float sumXY = 0, sumX = 0, sumY = 0, sumSquaredX = 0;
  for (int i = s->datapoints - 10; i < s->datapoints; i++) {
    sumXY += s->time_s[i] * s->weight[i];
    sumX += s->time_s[i];
    sumY += s->weight[i];
    sumSquaredX += s->time_s[i] * s->time_s[i];
  }
  float m = (10 * sumXY - sumX * sumY) / (10 * sumSquaredX - sumX * sumX);
  float b = (sumY - m * sumX) / 10;
  s->expected_end_s = (goalWeight - weightOffset - b) / m;
}

float seconds_f() {
  return millis() / 1000.0;
}

