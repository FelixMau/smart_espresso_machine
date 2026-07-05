#include "pid_controller.h"

#include "debug.h"

PIDController::PIDController(float kp, float ki, float kd)
  : kp(kp), ki(ki), kd(kd),
    outputMin(77),   // 30% floor: min power to prevent backflow/pump shutoff
    outputMax(255),  // Full power maximum
    integralMax(100.0f),
    integral(0.0f),
    previousError(0.0f),
    lastTimeMs(0),
    lastPTerm(0.0f),
    lastITerm(0.0f),
    lastDTerm(0.0f),
    lastOutput(0) {}

void PIDController::reset() {
  integral = 0.0f;
  previousError = 0.0f;
  lastTimeMs = millis();
  lastPTerm = 0.0f;
  lastITerm = 0.0f;
  lastDTerm = 0.0f;
  lastOutput = 0;
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

  // Derivative term (on error)
  float derivative = (error - previousError) / dt;
  float dTerm = kd * derivative;

  // Save state for next iteration
  previousError = error;
  lastTimeMs = now;

  // Calculate output: center at 128, add PID correction
  float output = 128.0f + pTerm + iTerm + dTerm;

  // Constrain to safe PWM range
  int pwmValue = constrain((int)round(output), outputMin, outputMax);

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
