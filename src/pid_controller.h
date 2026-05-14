#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <Arduino.h>
#include "debug.h"

// ============================================================================
// PID CONTROLLER FOR PRESSURE CONTROL
// ============================================================================
// Replaces simple proportional control with full PID to prevent:
// - Overshooting (damped by derivative term)
// - Pump shutoff (enforced minimum PWM floor)
// - Steady-state error (eliminated by integral term)

class PIDController {
public:
  // PID gains
  float kp;  // Proportional gain
  float ki;  // Integral gain
  float kd;  // Derivative gain

  // Output limits
  int outputMin;  // Minimum output (prevents pump shutoff)
  int outputMax;  // Maximum output

  // Anti-windup limit for integral term
  float integralMax;

  PIDController(float kp = 25.0f, float ki = 0.5f, float kd = 8.0f)
    : kp(kp), ki(ki), kd(kd),
      outputMin(60),      // ~24% power minimum to prevent backflow
      outputMax(255),     // Full power maximum
      integralMax(100.0f),
      integral(0.0f),
      previousError(0.0f),
      lastTimeMs(0) {}

  // Reset PID state (call when starting a new shot)
  void reset() {
    integral = 0.0f;
    previousError = 0.0f;
    lastTimeMs = millis();
  }

  // Calculate PID output
  // setpoint: target pressure (bar)
  // measurement: current pressure (bar)
  // Returns: PWM value (outputMin to outputMax)
  int calculate(float setpoint, float measurement) {
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

    DEBUG_ENCODER_PRINT("PID: err=%.2f P=%.1f I=%.1f D=%.1f -> PWM=%d",
                        error, pTerm, iTerm, dTerm, pwmValue);

    return pwmValue;
  }

  // Get current integral value (for debugging/tuning)
  float getIntegral() const { return integral; }

  // Manually adjust integral (for bumpless transfer or preloading)
  void setIntegral(float value) {
    integral = constrain(value, -integralMax, integralMax);
  }

private:
  float integral;        // Accumulated error
  float previousError;   // Previous error for derivative
  unsigned long lastTimeMs;  // Last calculation time
};

#endif // PID_CONTROLLER_H
