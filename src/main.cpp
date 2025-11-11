#include <Arduino.h>
#include <ESP32Encoder.h>

ESP32Encoder encoder;

const int encoderPinA = 23;    // Encoder pin A
const int encoderPinB = 25;    // Encoder pin B
const int dimmerPin = 5;       // PWM output pin
const int freq = 50;           // PWM frequency (Hz)
const int pwmChannel = 0;      // PWM channel
const int resolution = 8;      // 8-bit resolution (0 to 255)

void setup() {
  Serial.begin(9600);

  //ESP32Encoder::useInternalWeakPullResistors = up;
  encoder.attachHalfQuad(encoderPinA, encoderPinB);
  encoder.setCount(0);

  ledcSetup(pwmChannel, freq, resolution);
  ledcAttachPin(dimmerPin, pwmChannel);
}

void loop() {
  long newPosition = encoder.getCount();

  Serial.print("Encoder Position: ");
  Serial.println(newPosition);

  int pwmValue = constrain(newPosition, 0, 255);
  ledcWrite(pwmChannel, pwmValue);

  Serial.print("PWM Value: ");
  Serial.println(pwmValue);

  delay(100); // For readable output
}
