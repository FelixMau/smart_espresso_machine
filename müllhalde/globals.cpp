#include "globals.h"
#include <WebServer.h> // Ensure WebServer is included

// Shared global variables
AcaiaArduinoBLE scale(false);
float currentWeight = 0;
uint8_t goalWeight = 0;
float weightOffset = 0;
int buttonArr[BUTTON_STATE_ARRAY_LENGTH] = {0};
bool buttonPressed = false;
bool buttonLatched = false;
unsigned long lastButtonRead_ms = 0;
int newButtonState = 0;
bool LED0 = false;
bool SomeOutput = false;
int FanSpeed = 0;
uint32_t SensorUpdate = 0;
char XML[2048];
char buf[32];
Shot shot = {0, 0, 0, 0, {}, {}, 0, false, Shot::UNDEF};
BLEService weightService("0x0FFE");
BLEByteCharacteristic weightCharacteristic("0xFF11", BLEWrite | BLERead);
WebServer server(80); // Correct definition of WebServer

// RGB Colors
int RED[3] = {255, 0, 0};
int GREEN[3] = {0, 255, 0};
int BLUE[3] = {0, 0, 255};
int OFF[3] = {0, 0, 0};
int currentColor[3] = {0, 0, 0};

int FanRPM = 0; // Define FanRPM
const int EEPROM_SIZE = 512; // Define EEPROM_SIZE
const int WEIGHT_ADDR = 0; // Define WEIGHT_ADDR
const int OFFSET_ADDR = 1; // Define OFFSET_ADDR
const int MAX_OFFSET = 5; // Define MAX_OFFSET
const int IN = 35; // Define IN pin
const int OUT = 14; // Define OUT pin
const int PIN_FAN = 27; // Define PIN_FAN
const int PIN_LED = 2; // Define PIN_LED
const int PIN_OUTPUT = 26; // Define PIN_OUTPUT
const int PIN_A0 = 34; // Define PIN_A0
