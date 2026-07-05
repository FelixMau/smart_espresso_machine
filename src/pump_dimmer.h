#ifndef PUMP_DIMMER_H
#define PUMP_DIMMER_H

// ============================================================================
// PUMP DIMMER - AC PHASE-ANGLE CONTROL (leading-edge / triac)
// ============================================================================
// Replaces the old plain 50 Hz LEDC PWM on the dimmer pin with proper
// phase-angle control synchronized to the mains zero crossings:
//
//   zero cross (GPIO 4, 100/s at 50 Hz)
//     -> gate LOW (triac stops conducting at the zero crossing by itself)
//     -> one-shot hardware timer armed with the firing delay
//   timer fires
//     -> gate HIGH; held high until the next zero cross so the triac cannot
//        drop out on the inductive pump load
//
// Firing delay is linear in the power level (rbdimmer.com formula):
//   delay = (255 - level) / 255 * 10 ms
//
// The power API keeps the 0-255 scale of the old ledcWrite() call, so the
// PID controller and the web dashboard are unaffected.
//
// Graceful degradation: if no zero crossings arrive (sensor unplugged, bench
// setup without mains), the module falls back to plain on/off - any nonzero
// level drives the gate solid HIGH (full power), so a lost sync signal can
// never kill the pump mid-shot.

#include <Arduino.h>

#define ZERO_CROSS_PIN 4  // Zero-cross detector output (rising edge per crossing)

// Configure pins, hardware timer and the zero-cross interrupt.
// gatePin is the triac gate / opto-coupler drive (DIMMER_PIN).
void initPumpDimmer(int gatePin);

// Set pump power 0-255 (same scale the PID emits). Applied from the next
// mains half-cycle onwards. Also services the no-zero-cross fallback, so
// call it periodically (the control task does, every ~50 ms).
void pumpDimmerSetPower(uint8_t level);

// True while mains zero crossings are arriving on ZERO_CROSS_PIN
bool pumpDimmerZcHealthy();

#endif // PUMP_DIMMER_H
