#ifndef PUMP_DIMMER_H
#define PUMP_DIMMER_H

// ============================================================================
// PUMP DIMMER - ZERO-CROSS SYNCED TRIAC CONTROL (PSM or phase-angle)
// ============================================================================
// Two firing schemes, selected by PUMP_PSM_MODE, both synchronized to the
// mains zero crossings on ZERO_CROSS_PIN (GPIO 4, 100/s at 50 Hz):
//
// PSM (pulse-skip modulation, gaggiuino-style, default): the vibratory pump
// rectifies half-wave internally and does exactly one piston stroke per
// conducted mains cycle. A Bresenham accumulator decides once per full cycle
// (every 2nd zero cross) whether to conduct it whole (gate HIGH, one pump
// stroke, "click") or skip it (gate LOW). Power = fraction of cycles fired,
// spread as evenly as possible; conducted strokes are counted so the pump
// doubles as a flow meter (pump_model.cpp).
//
// Phase-angle (leading edge): the zero cross drops the gate and arms a
// one-shot hardware timer with delay = (255 - level) / 255 * 10 ms; the timer
// raises the gate, held high until the next zero cross so the triac cannot
// drop out on the inductive pump load.
//
// The power API keeps the 0-255 scale of the old ledcWrite() call, so the
// controllers and the web dashboard are unaffected.
//
// Graceful degradation: if no zero crossings arrive (sensor unplugged, bench
// setup without mains), the module falls back to plain on/off - any nonzero
// level drives the gate solid HIGH (full power), so a lost sync signal can
// never kill the pump mid-shot.

#include <Arduino.h>

// Pulse-skip modulation (whole-cycle firing + click counting) instead of
// phase-angle. Right for vibratory pumps (ULKA/CEME style, as in the Dalla
// Corte Mini); use phase-angle (false) for rotary pumps.
#define PUMP_PSM_MODE true

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

// Cumulative count of conducted mains cycles (= pump strokes in PSM mode).
// Callers keep their own last value and diff; the counter is never reset.
uint32_t pumpDimmerClickCount();

#endif // PUMP_DIMMER_H
