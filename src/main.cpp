#include <Arduino.h>
#include "rbdimmerESP32.h"


const int dimmerPin = 5;      // Choose your GPIO pin
const int freq = 50;         // PWM frequency, usually 1kHz-5kHz for basic use
const int pwmChannel = 0;      // PWM channel (0-7 available)
const int resolution = 8;      // 8-bit resolution (0 to 255)

void setup() {
  ledcSetup(pwmChannel, freq, resolution);
  ledcAttachPin(dimmerPin, pwmChannel);
  Serial.begin(9600);
}

void loop() {
  for(int dutyCycle = 100; dutyCycle <= 255; dutyCycle++) {
    ledcWrite(pwmChannel, dutyCycle);
    delay(10);
    Serial.println(dutyCycle);
    
  }
  for(int dutyCycle = 255; dutyCycle >= 100; dutyCycle--) {
    ledcWrite(pwmChannel, dutyCycle);
    delay(10);
    Serial.println(dutyCycle);
  }
}