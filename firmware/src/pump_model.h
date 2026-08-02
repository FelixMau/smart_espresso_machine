#ifndef PUMP_MODEL_H
#define PUMP_MODEL_H

// ============================================================================
// VIBRATORY PUMP MODEL + FEEDFORWARD PRESSURE CONTROL
// ============================================================================
// Ported from the gaggiuino project (GPLv3, src/peripherals/pump.cpp at
// release/stm32-blackpill). The pump does one piston stroke ("click") per
// conducted mains cycle, and the volume each click moves is a known function
// of the system pressure (CEME E5-60 curve, empirically tuned:
// https://www.desmos.com/calculator/axyl70gjae). Counting conducted cycles
// (pump_dimmer.cpp, PSM mode) therefore measures flow without a flow sensor.
//
// The control law is NOT a PID: the baseline output is the click rate that
// sustains the current measured flow at the current pressure (model-based
// feedforward, which adapts to the puck as the shot evolves) plus a small
// proportional trim. Overpressure cuts the pump instead of waiting for an
// integral to unwind; large errors approach on a bounded ramp.

// One pump stroke per mains cycle at 50 Hz
#define MAX_PUMP_CLICKS_PER_SECOND 50

// EMA factor for the click-rate flow estimate, applied per 100 ms window
// (DERIVED_STATE_PERIOD_MS): clicks arrive quantized (0-5 per window), so
// smoothing is required; 0.25 at 10 Hz is a ~350 ms time constant
#define PUMP_FLOW_FILTER_ALPHA 0.25f

// Volume one pump stroke moves at the given pressure (ml/click)
float getPumpFlowPerClick(float pressureBar);

// Model-estimated pump flow (ml/s) from the measured click rate
float getPumpFlow(float clicksPerSecond, float pressureBar);

// Click rate needed to push the given flow at the given pressure,
// capped at MAX_PUMP_CLICKS_PER_SECOND
float getClicksPerSecondForFlow(float flowMlPerS, float pressureBar);

// Fraction of the maximum click rate (0..1) to reach/hold targetPressure.
// flowRestriction (ml/s) caps the output for flow-limited profiles; pass 0
// for no cap. smoothedPumpFlow and pressureChangeSpeed come from the control
// task (main.cpp), computed from the click counter and the filtered pressure.
float getPumpPct(float targetPressure, float flowRestriction,
                 float smoothedPressure, float smoothedPumpFlow,
                 float pressureChangeSpeed);

#endif // PUMP_MODEL_H
