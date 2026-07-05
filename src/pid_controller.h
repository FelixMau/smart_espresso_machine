#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <Arduino.h>

// ============================================================================
// PID CONTROLLER FOR PRESSURE CONTROL
// ============================================================================
// Replaces simple proportional control with full PID to prevent:
// - Overshooting (damped by derivative term)
// - Pump shutoff (enforced minimum PWM floor)
// - Steady-state error (eliminated by integral term)

class PIDController {
public:
  // PID gains (public so the web dashboard can tune them live)
  float kp;  // Proportional gain
  float ki;  // Integral gain
  float kd;  // Derivative gain

  // Output limits
  int outputMin;  // Minimum output (prevents pump shutoff)
  int outputMax;  // Maximum output

  // Anti-windup limit for integral term
  float integralMax;

  PIDController(float kp = 25.0f, float ki = 0.5f, float kd = 8.0f);

  // Reset PID state (call when starting a new shot)
  void reset();

  /**
   * @brief Calculate the PID output for one control iteration.
   * @param setpoint Target pressure (bar).
   * @param measurement Current pressure (bar).
   * @return PWM value constrained to [outputMin, outputMax].
   */
  int calculate(float setpoint, float measurement);

  // Last computed terms and output (for web dashboard / tuning)
  float getPTerm() const { return lastPTerm; }
  float getITerm() const { return lastITerm; }
  float getDTerm() const { return lastDTerm; }
  int getOutput() const { return lastOutput; }

  // Get current integral value (for debugging/tuning)
  float getIntegral() const { return integral; }

  // Manually adjust integral (for bumpless transfer or preloading)
  void setIntegral(float value);

private:
  float integral;            // Accumulated error
  float previousError;       // Previous error for derivative
  unsigned long lastTimeMs;  // Last calculation time
  float lastPTerm;           // Last proportional term (monitoring)
  float lastITerm;           // Last integral term (monitoring)
  float lastDTerm;           // Last derivative term (monitoring)
  int lastOutput;            // Last constrained PWM output (monitoring)
};

#endif // PID_CONTROLLER_H
