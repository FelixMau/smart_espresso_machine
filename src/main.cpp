#include <Arduino.h>
#include <Encoder.h>

Encoder myEnc(32, 35);  // Connect encoder to pins 2 and 3

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
  long newPosition = myEnc.read();
  // Map position to 0-255
  Serial.print("Encoder Position: ");
  Serial.println(newPosition);

  int pwmValue = constrain(newPosition, 0, 255);
  ledcWrite(pwmChannel, pwmValue);
  Serial.print("PWM Value: ");
  Serial.println(pwmValue);
}