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
//
// Smoothness measures (the pump must never slam between power levels):
// - Derivative acts on the measurement, not the error, so setpoint steps
//   from the pressure profile don't kick the output
// - The derivative is low-pass filtered against residual sensor noise
// - The output is slew-rate limited to outputSlewRate counts per second

// Low-pass factor for the derivative term (EMA per iteration at ~20 Hz)
#define PID_DERIVATIVE_FILTER_ALPHA 0.3f

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

  // Maximum output change in PWM counts per second (full range in ~0.6 s):
  // fast enough for profile steps, slow enough that the pump ramps smoothly
  float outputSlewRate;

  PIDController(float kp = 15.0f, float ki = 1.0f, float kd = 5.0f);

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
  float previousMeasurement; // Previous measurement for derivative-on-measurement
  float derivativeFiltered;  // Low-pass filtered derivative
  bool firstSample;          // No previous measurement yet (after reset)
  unsigned long lastTimeMs;  // Last calculation time
  float lastPTerm;           // Last proportional term (monitoring)
  float lastITerm;           // Last integral term (monitoring)
  float lastDTerm;           // Last derivative term (monitoring)
  int lastOutput;            // Last constrained PWM output (monitoring)
};

#endif // PID_CONTROLLER_H
