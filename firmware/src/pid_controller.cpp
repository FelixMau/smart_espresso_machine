#include "pid_controller.h"

#include "debug.h"

PIDController::PIDController(float kp, float ki, float kd)
  : kp(kp), ki(ki), kd(kd),
    outputMin(0),   // 30% floor: min power to prevent backflow/pump shutoff
    outputMax(255),  // Full power maximum
    integralMax(100.0f),
    outputSlewRate(300.0f),
    integral(0.0f),
    previousMeasurement(0.0f),
    derivativeFiltered(0.0f),
    firstSample(true),
    lastTimeMs(0),
    lastPTerm(0.0f),
    lastITerm(0.0f),
    lastDTerm(0.0f),
    lastOutput(255) {}

void PIDController::reset() {
  integral = 0.0f;
  derivativeFiltered = 0.0f;
  firstSample = true;
  lastTimeMs = millis();
  lastPTerm = 0.0f;
  lastITerm = 0.0f;
  lastDTerm = 0.0f;
  lastOutput = outputMax;  // Pump idles at 100%; slew down from there at shot start
}

int PIDController::calculate(float setpoint, float measurement) {
  unsigned long now = millis();
  float dt = (now - lastTimeMs) / 1000.0f;  // Convert to seconds

  // Prevent division by zero on first call or very fast loops
  if (dt <= 0.0f || lastTimeMs == 0) {
    lastTimeMs = now;
    return outputMax;  // Default to full power on first call
  }

  // Calculate error (positive = need more pressure)
  float error = setpoint - measurement;

  // Proportional term
  float pTerm = kp * error;

  // Integral term with anti-windup clamping
  integral += error * dt;
  integral = constrain(integral, -integralMax, integralMax);
  float iTerm = ki * integral;

  // Derivative on measurement (sign-flipped), so setpoint steps from the
  // pressure profile don't kick the output; low-pass filtered against noise
  float derivative = firstSample ? 0.0f : -(measurement - previousMeasurement) / dt;
  firstSample = false;
  derivativeFiltered += PID_DERIVATIVE_FILTER_ALPHA * (derivative - derivativeFiltered);
  float dTerm = kd * derivativeFiltered;

  // Save state for next iteration
  previousMeasurement = measurement;
  lastTimeMs = now;

  // Calculate output: center at 128, add PID correction
  float output = 128.0f + pTerm + iTerm + dTerm;

  // Constrain to safe PWM range
  int pwmValue = constrain((int)round(output), outputMin, outputMax);

  // Slew-rate limit: ramp instead of jumping between power levels
  int maxStep = max(1, (int)(outputSlewRate * dt));
  pwmValue = constrain(pwmValue, lastOutput - maxStep, lastOutput + maxStep);
  pwmValue = constrain(pwmValue, outputMin, outputMax);

  // Store terms for external monitoring (web dashboard)
  lastPTerm = pTerm;
  lastITerm = iTerm;
  lastDTerm = dTerm;
  lastOutput = pwmValue;

  DEBUG_ENCODER_PRINT("PID: err=%.2f P=%.1f I=%.1f D=%.1f -> PWM=%d",
                      error, pTerm, iTerm, dTerm, pwmValue);

  return pwmValue;
}

void PIDController::setIntegral(float value) {
  integral = constrain(value, -integralMax, integralMax);
}
