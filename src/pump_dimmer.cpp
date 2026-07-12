#include "pump_dimmer.h"

#include "debug.h"

// ============================================================================
// TIMING CONSTANTS (50 Hz mains)
// ============================================================================

// One half-cycle, zero cross to zero cross: 10 ms at 50 Hz
static const uint32_t HALF_CYCLE_US = 10000;

// Zero-cross detectors ring/bounce around the crossing; edges arriving
// sooner than this after an accepted crossing are glitches, not crossings
static const uint32_t ZC_GLITCH_US = 4000;

// Never fire the gate closer than this to a zero crossing. Too early and the
// detector's pulse width blurs which half-cycle we are in; too late and the
// triac has no time to latch before the current stops again.
static const uint32_t MIN_FIRE_DELAY_US = 200;
static const uint32_t MAX_FIRE_DELAY_US = HALF_CYCLE_US - 500;

// Levels at the extremes skip the timer entirely: >= FULL_ON_LEVEL holds the
// gate high through the whole cycle, <= FULL_OFF_LEVEL never fires it
static const uint8_t FULL_ON_LEVEL = 250;
static const uint8_t FULL_OFF_LEVEL = 2;

// No crossing for this long means the sync signal is gone (10 missed
// half-cycles) - fall back to plain on/off control
static const uint32_t ZC_TIMEOUT_US = 100000;

// ============================================================================
// STATE
// ============================================================================

static int gatePin = -1;
static hw_timer_t* fireTimer = nullptr;

// Written by the control task, read by the zero-cross ISR (single byte, atomic)
static volatile uint8_t powerLevel = 255;

// Timestamp of the last accepted zero crossing (glitch filter + health check)
static volatile uint32_t lastZcUs = 0;

#if PUMP_PSM_MODE
// Bresenham accumulator: fire a cycle whenever it wraps past the full range,
// so any level 0-255 spreads its fired cycles as evenly as possible
static const uint16_t PSM_RANGE = 255;
static volatile uint16_t psmAccumulator = 0;
static volatile bool psmSecondHalf = false;   // Which half of the mains cycle
static volatile uint32_t psmClickCount = 0;   // Conducted cycles = pump strokes
#endif

// ============================================================================
// INTERRUPT HANDLERS
// ============================================================================

// Firing-delay timer expired: turn the triac on. The gate stays high until
// the next zero cross, so the triac keeps conducting even when the inductive
// pump load makes the current lag the voltage.
static void IRAM_ATTR onFireTimer() {
  digitalWrite(gatePin, HIGH);
}

// Mains zero crossing: drop the gate and arm the one-shot firing timer.
// Registered without the IRAM flag, so it is simply deferred during flash
// writes (EEPROM.commit) instead of crashing on the flash-resident timer HAL.
static void onZeroCross() {
  uint32_t now = micros();
  if (now - lastZcUs < ZC_GLITCH_US) {
    return;  // Ringing on the detector edge, not a real crossing
  }
  lastZcUs = now;

#if PUMP_PSM_MODE
  // The pump strokes once per full mains cycle (internal half-wave
  // rectification), so decide once per cycle and hold the gate through both
  // half-cycles; which polarity actually drives the coil doesn't matter.
  psmSecondHalf = !psmSecondHalf;
  if (psmSecondHalf) {
    return;
  }

  psmAccumulator += powerLevel;
  if (psmAccumulator >= PSM_RANGE) {
    psmAccumulator -= PSM_RANGE;
    digitalWrite(gatePin, HIGH);  // Conduct this cycle: one pump stroke
    psmClickCount++;
  } else {
    digitalWrite(gatePin, LOW);   // Skip this cycle
  }
  return;
#endif

  uint8_t level = powerLevel;
  if (level >= FULL_ON_LEVEL) {
    // Full power: hold the gate high, triac conducts the entire half-cycle
    digitalWrite(gatePin, HIGH);
    return;
  }

  digitalWrite(gatePin, LOW);  // Triac commutates off at the zero crossing
  if (level <= FULL_OFF_LEVEL) {
    return;  // Pump off: never fire this half-cycle
  }

  uint32_t delayUs = ((uint32_t)(255 - level) * HALF_CYCLE_US) / 255;
  delayUs = constrain(delayUs, MIN_FIRE_DELAY_US, MAX_FIRE_DELAY_US);

  timerRestart(fireTimer);
  timerAlarmWrite(fireTimer, delayUs, false);  // One-shot
  timerAlarmEnable(fireTimer);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void initPumpDimmer(int pin) {
  gatePin = pin;
  pinMode(gatePin, OUTPUT);
  digitalWrite(gatePin, LOW);

  // Pull-up covers open-collector detector outputs; harmless for push-pull
  pinMode(ZERO_CROSS_PIN, INPUT_PULLUP);

  // Timer 0, 80 MHz / 80 = 1 tick per microsecond
  fireTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(fireTimer, &onFireTimer, true);

  attachInterrupt(digitalPinToInterrupt(ZERO_CROSS_PIN), onZeroCross, RISING);

  DEBUG_PUMP_PRINT("%s dimmer initialized (gate GPIO %d, zero cross GPIO %d)",
                   PUMP_PSM_MODE ? "Pulse-skip (PSM)" : "Phase-angle",
                   gatePin, ZERO_CROSS_PIN);
}

bool pumpDimmerZcHealthy() {
  return (micros() - lastZcUs) < ZC_TIMEOUT_US;
}

uint32_t pumpDimmerClickCount() {
#if PUMP_PSM_MODE
  return psmClickCount;
#else
  return 0;
#endif
}

void pumpDimmerSetPower(uint8_t level) {
  powerLevel = level;

  // Log sync gain/loss once per transition (called from the control task)
  static bool wasHealthy = false;
  bool healthy = pumpDimmerZcHealthy();
  if (healthy != wasHealthy) {
    wasHealthy = healthy;
    if (healthy) {
      DEBUG_PUMP_PRINT("Zero-cross sync acquired - synced pump dimming active");
    } else {
      DEBUG_PUMP_PRINT("No zero crossings on GPIO %d - falling back to on/off pump control",
                       ZERO_CROSS_PIN);
    }
  }

  // Fallback when no zero crossings arrive (detector unplugged, bench setup
  // without mains): degrade to on/off so the pump never dies mid-shot. With a
  // random-fire opto-triac a solid HIGH gate means full conduction.
  if (!healthy) {
    digitalWrite(gatePin, level > FULL_OFF_LEVEL ? HIGH : LOW);
  }
}
