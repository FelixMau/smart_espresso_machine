#ifndef GLOBALS_H
#define GLOBALS_H

#include <WebServer.h> // Ensure WebServer is included
#include <AcaiaArduinoBLE.h>
#include "shotStopper.h"

// Shared global variables
extern AcaiaArduinoBLE scale;
extern float currentWeight;
extern uint8_t goalWeight;
extern float weightOffset;
extern int buttonArr[];
extern bool buttonPressed;
extern bool buttonLatched;
extern unsigned long lastButtonRead_ms;
extern int newButtonState;
extern bool LED0;
extern bool SomeOutput;
extern int FanSpeed;
extern uint32_t SensorUpdate;
extern char XML[];
extern char buf[];
extern Shot shot;
extern BLEService weightService;
extern BLEByteCharacteristic weightCharacteristic;
extern WebServer server; // Correct declaration of WebServer
extern int FanRPM;
extern const int EEPROM_SIZE;
extern const int WEIGHT_ADDR;
extern const int OFFSET_ADDR;
extern const int MAX_OFFSET;
extern const int IN;
extern const int OUT;
extern const int PIN_FAN;
extern const int PIN_LED;
extern const int PIN_OUTPUT;
extern const int PIN_A0;

// RGB Colors
extern int RED[];
extern int GREEN[];
extern int BLUE[];
extern int OFF[];
extern int currentColor[];

// Function declarations for shared functions
void SendWebsite();
void SendXML();
void UpdateSlider();
void ProcessButton_0();
void ProcessButton_1();

#endif // GLOBALS_H
