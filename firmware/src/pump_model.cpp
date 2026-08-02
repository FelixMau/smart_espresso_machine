#include "pump_model.h"

#include <Arduino.h>

// Flow of one click with no pressure in the system (ml). Gaggiuino default
// for the ULKA EX5; tune against measured shot weights if needed.
static const float FLOW_PER_CLICK_AT_ZERO_BAR = 0.27f;

// Click volume in the model is per minute-normalized click; at 50 clicks/s
// this rescales to the datasheet's ml/min curve (gaggiuino: 60 / maxCPS)
static const float FPC_MULTIPLIER = 60.0f / MAX_PUMP_CLICKS_PER_SECOND;

// Pressure-inefficiency polynomial, gaggiuino's fitted curve (blue curve at
// https://www.desmos.com/calculator/axyl70gjae)
static const float C0 = 0.045f;
static const float C1 = 0.015f;
static const float C2 = 0.0033f;
static const float C3 = 0.000685f;
static const float C4 = 0.000045f;
static const float C5 = 0.009f;
static const float C6 = -0.0018f;

float getPumpFlowPerClick(float pressureBar) {
  const float p = pressureBar;
  // Same polynomial as gaggiuino's, with their (C5/p + C6) * -p^2 term
  // expanded to -C5*p - C6*p^2 so p = 0 doesn't divide by zero
  float fpc = -C5 * p - C6 * p * p
              + (FLOW_PER_CLICK_AT_ZERO_BAR - C0)
              - (C1 + (C2 - (C3 - C4 * p) * p) * p) * p;
  return fpc * FPC_MULTIPLIER;
}

float getPumpFlow(float clicksPerSecond, float pressureBar) {
  return clicksPerSecond * getPumpFlowPerClick(pressureBar);
}

float getClicksPerSecondForFlow(float flowMlPerS, float pressureBar) {
  if (flowMlPerS <= 0.0f) {
    return 0.0f;
  }
  float flowPerClick = getPumpFlowPerClick(pressureBar);
  if (flowPerClick <= 0.001f) {
    // Beyond the pump's deadhead pressure no click moves water; asking for
    // flow here means full rate (the cap) rather than a negative rate
    return (float)MAX_PUMP_CLICKS_PER_SECOND;
  }
  return fminf(flowMlPerS / flowPerClick, (float)MAX_PUMP_CLICKS_PER_SECOND);
}

float getPumpPct(float targetPressure, float flowRestriction,
                 float smoothedPressure, float smoothedPumpFlow,
                 float pressureChangeSpeed) {
  if (targetPressure == 0.0f) {
    return 0.0f;
  }

  float diff = targetPressure - smoothedPressure;
  float maxPumpPct = flowRestriction <= 0.0f
      ? 1.0f
      : getClicksPerSecondForFlow(flowRestriction, smoothedPressure)
          / (float)MAX_PUMP_CLICKS_PER_SECOND;
  // Feedforward baseline: the click rate that sustains the current measured
  // flow at the current pressure - tracks the puck's resistance by itself
  float pumpPctToMaintainFlow =
      getClicksPerSecondForFlow(smoothedPumpFlow, smoothedPressure)
          / (float)MAX_PUMP_CLICKS_PER_SECOND;

  if (diff > 2.0f) {
    // Far below target: bounded approach ramp instead of a full-power slam
    return fminf(maxPumpPct, 0.25f + 0.2f * diff);
  }
  if (diff > 0.0f) {
    // Near target: hold what the puck currently takes, plus a small trim
    return fminf(maxPumpPct, pumpPctToMaintainFlow * 0.95f + 0.1f + 0.2f * diff);
  }
  if (pressureChangeSpeed < 0.0f) {
    // Above target but already falling: keep a trickle for a soft landing
    return fminf(maxPumpPct, pumpPctToMaintainFlow * 0.2f);
  }
  // Above target and rising: pump off, let the puck bleed the pressure down
  return 0.0f;
}
