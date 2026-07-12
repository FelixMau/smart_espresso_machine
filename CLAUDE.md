# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Store one lesson per file with a one-line summary at the top. Record corrections and confirmed approaches alike, including why they mattered. Don't save what the repo or chat history already records; update an existing note rather than creating a duplicate; delete notes that turn out to be wrong.
Reflect on the previous sessions we've had together. Use subagents to identify core themes and lessons, and store them in .claude/ Make sure you know to reference files in folder .claude/ for future use.

## Quick Start Commands

```bash
# Build firmware
platformio run -e upesy_wroom

# Upload to device
platformio run -e upesy_wroom --target upload

# Monitor serial output (115200 baud; DEBUG_ENABLED in debug.h is off by default - serial prints throttle the 100 Hz control loop)
platformio device monitor

# Run tests
platformio test
```

## Project Overview

**Smart Espresso Machine** is an embedded IoT retrofit that automates espresso shot brewing on existing machines (currently a Dalla Corte Mini) using:
- **ESP32 microcontroller** (upesy_wroom) with custom PCB integration
- **Bluetooth scale** (Acaia Lunar) for weight measurement via BLE
- **Pressure sensor** (MPX5500, 0-16 bar) for extraction monitoring
- **Opto-isolated button control** to automate pump and solenoid
- **PID-controlled pump dimmer** (zero-cross-synced phase-angle triac control) following configurable pressure profiles
- **Rotary encoder** for manual pump power adjustment
- **Async web dashboard** for live monitoring, control, and PID tuning over LAN
- **EEPROM storage** for goal weight, learned calibration offsets, pressure profile, cleaning config and WiFi credentials (settings.h/.cpp)

Design philosophy (vs. e.g. Gaggiuino): **extend** the machine's original controls instead of replacing them — no high-voltage control logic replacement, machine stays restorable to stock.

The system uses **linear regression** to predict when shots reach target weight and automatically stops extraction at the optimal moment.

## Architecture

### Source Layout (everything lives in `src/`)

Modules are `.h`/`.cpp` pairs: declarations (types, `extern` globals, function
prototypes) in the header, definitions in the source file.

| File | Lines | Purpose |
|------|-------|---------|
| `main.cpp` | ~420 | Entry point: setup, FreeRTOS tasks (control + scale), WiFi watchdog loop |
| `shot_stopper.h/.cpp` | ~180/~340 | Core brewing logic: `Shot` struct, button state machine, regression, EEPROM learning |
| `webserver.h/.cpp` | ~35/~350 | Async REST API (`/state`, start/stop, PID tuning, pressure profiles, shot history) |
| `dashboard.h` | ~340 | Embedded single-page dashboard (PROGMEM HTML, Chart.js from CDN, polls `/state` every 500 ms) |
| `pid_controller.h/.cpp` | ~65/~75 | PID class for pressure control (anti-windup, output floor to prevent pump shutoff) |
| `shot_history.h/.cpp` | ~45/~70 | RAM-only ring buffer of last 5 shots, downsampled to 100 points each |
| `pump_dimmer.h/.cpp` | ~55/~160 | Zero-cross synced pump dimmer (GPIO 4 ISR): PSM whole-cycle firing with click counting (`PUMP_PSM_MODE`, default) or phase-angle via one-shot hw timer; falls back to on/off if the sync signal disappears |
| `pump_model.h/.cpp` | ~45/~85 | Vibratory pump model + gaggiuino-style feedforward pressure control (flow per click as f(pressure), `getPumpPct` control law) |
| `settings.h/.cpp` | ~65/~160 | Versioned EEPROM settings blob: goal weight, offset, pressure profile, cleaning config, WiFi credentials; legacy two-byte migration |
| `cleaning_cycle.h/.cpp` | ~90/~250 | Automated detergent backflush: pressure-limited flushes (fill to max bar → hold at dimmed pump → release), soak, user-confirmed rinse phase |
| `debug.h` | ~70 | Category-based serial debug macros (`DEBUG_SHOT_PRINT`, `DEBUG_SCALE_PRINT`, ...) |
| `secrets.h` | 1 | WiFi credentials (`ssid`, `password`) — must NOT be committed. Include AFTER the WiFi headers: the macros clobber their parameter names otherwise |

### FreeRTOS Task Structure (main.cpp)

- **controlTask** (core 1, priority 2, ~10 ms cadence / 100 Hz): scale-data consumption, shot trajectory updates, pump pressure control, button state machine, shot end detection, EEPROM learning. Never blocks on BLE or HTTP.
- **scaleTask** (core 1, priority 1, ~20 ms cadence): owns ALL BLE/scale calls (init with 10 s scan, heartbeat, weight polling, tare/timer commands). 5 s backoff between failed connect attempts because BLE scanning and WiFi share the 2.4 GHz radio.
- **loop()** (default loopTask): WiFi watchdog only — reconnects every 15 s and starts the web server late if WiFi came up after boot.
- **AsyncTCP task** (core 0): HTTP handlers. They only set `volatile` request flags (`webStartRequest` etc.) consumed by controlTask — handlers never touch scale/BLE/GPIO directly.

Cross-task communication is via `volatile` flags declared in shot_stopper.h / webserver.h (`scaleConnected`, `scaleNewWeight`, `scaleTareRequest`, `webStartRequest`, `webStopRequest`, `webResetRequest`, ...).

`TESTING_MODE_NO_SCALE` in main.cpp disables BLE entirely for bench testing without the scale.

### Core Data Structure: `struct Shot` (shot_stopper.h)

Central state object tracking:
- Brew timing (`shotTimer`, `expectedEndS`, `endS`)
- Weight measurements (`weight[MAX_SHOT_DATAPOINTS]`, `timeS[MAX_SHOT_DATAPOINTS]`, `datapoints`)
- Pressure readings (`pressure`, `peakPressure`) and goals (`currentGoalPressure`, profiles)
- User parameters (`goalWeight`, `weightOffset`)
- Status (`brewing`, `pumpPwm`) and end reason (`enum class EndType`: `BUTTON`, `WEIGHT`, `TIME`, `WEB`, `UNDEF`)

A global `Shot shot` instance is shared across modules.

### Key Algorithms

**Linear Regression Prediction** (shot_stopper.cpp:calculateEndTime)
- Fits line to last `TREND_LINE_DATAPOINTS` (10) weight-vs-time datapoints
- Predicts: `expectedEndS = (goalWeight - weightOffset - b) / m`
- Only calculates after 10+ measurements and weight > 10 g to avoid erratic predictions
- Enables stopping shots before over-extraction

**Pressure Control** (main.cpp:controlIteration, `GAGGIUINO_PUMP_CONTROL` selects the law)
- Default (gaggiuino-style feedforward, pump_model.cpp): output = click rate that sustains the current model-estimated flow at the current pressure + small proportional trim (`getPumpPct`, ported from gaggiuino's pump.cpp). No integral (no windup/limit cycling); overpressure cuts the pump instead of unwinding; errors > 2 bar approach on a bounded ramp. Pump flow is estimated from the PSM click counter × `getPumpFlowPerClick(pressure)` (ULKA/CEME curve; `FLOW_PER_CLICK_AT_ZERO_BAR` = 0.27 ml is the tunable), EMA-smoothed, published as `shot.pumpFlow` (`/state` `pumpFlow`)
- Fallback (`GAGGIUINO_PUMP_CONTROL false`): PID (pid_controller.cpp), Kp=15/Ki=1/Kd=5, live-tunable via `/set_pid`, derivative-on-measurement, slew-limited output, output floor
- Pressure is sampled every control iteration (~100 Hz) with EMA smoothing (`PRESSURE_FILTER_ALPHA`) — never only at the scale's ~2 Hz weight rate, which froze the measurement between updates and made the old PID derivative slam the pump on every jump; dP/dt (`pressureChangeSpeed`) and the click-rate flow estimate are computed over 100 ms windows (`DERIVED_STATE_PERIOD_MS`)
- Idle state: pump at 100 % (level 255)

**Pump Dimming** (pump_dimmer.cpp, `PUMP_PSM_MODE` selects the firing scheme)
- Zero-cross detector on GPIO 4 (rising edge, 100/s at 50 Hz); triac gate on `DIMMER_PIN` (GPIO 5); 4 ms glitch filter on ZC edges
- PSM (default, for the vibratory pump): Bresenham accumulator decides once per full mains cycle (every 2nd ZC) whether to conduct it whole (gate HIGH both half-cycles = one pump stroke, counted in `pumpDimmerClickCount()`) or skip it; power = fraction of cycles fired, evenly spread. One stroke per cycle because the pump rectifies half-wave internally
- Phase-angle (`PUMP_PSM_MODE false`, for rotary pumps): ZC ISR drops the gate and arms a one-shot hw timer (timer 0, 1 µs ticks) with `delay = (255 − level)/255 × 10 ms`; firing window clamped to [200 µs, 9.5 ms]; levels ≥ 250 hold the gate high, ≤ 2 never fire
- Fallback: no zero crossings for 100 ms → plain on/off gate drive (pump survives a lost sync signal mid-shot); transitions logged via `DEBUG_PUMP_PRINT`

**Pressure Profiles** (shot_stopper.cpp:updateShotTrajectory)
- Two goal lists, up to `MAX_PRESSURE_GOALS` (8) each:
  - **By time**: seconds from shot start (default: 2 bar @ 0 s → 9 bar @ 5 s → 6 bar @ 20 s)
  - **By time-left**: overrides based on predicted remaining time (default: 4 bar for last 5 s)
- Set via web as comma lists; negative times encode "time left"

**Cleaning Cycle** (cleaning_cycle.cpp:cleaningUpdate, runs in controlTask)
- Standard detergent backflush automated (per La Marzocco/Cafiza practice: 5 flushes on/off with detergent, soak, 5 rinse flushes) but the on-phase is **pressure-limited, not timed**: with a blind basket the pump deadheads, so each flush fills until `maxPressureBar` (default 9), holds `holdS` (5 s) with the pump dimmed to `holdPumpLevel` (120), then releases the button so the 3-way valve vents detergent through the valve body
- `fillTimeoutS` (10 s) fallback if pressure never builds (blind basket forgotten — flagged on the dashboard); hard failsafe releases immediately at `maxPressureBar + 1`
- Flow: `/start_cleaning` → detergent flushes → `soakS` (60 s) soak → AWAIT_RINSE (user rinses basket, presses "Confirm rinse" → `/continue_cleaning`; 10 min timeout aborts) → rinse flushes → done. `/stop_cleaning` aborts anytime; a **physical button press aborts without re-pulsing** (the user already toggled the machine)
- Config live-tunable via `/set_cleaning` (RAM only); shot starts blocked while cleaning and vice versa

**Button State Machine** (shot_stopper.cpp:handleButtonLogic)
- Four states: IDLE → PRESSED → [HELD] → RELEASED
- Debouncing via 8-sample majority array read every 5 ms
- Supports two machine types (`MOMENTARY` define):
  - **Momentary switches** (GS3 AV): press toggles brew on/off; system pulses OUT for 1 s to stop
  - **Latching switches** (Linea Mini): press starts, system latches OUT after `MIN_SHOT_DURATION_S`
- `REEDSWITCH` alternative input supported

**EEPROM Auto-Learning** (shot_stopper.cpp:detectShotError)
- Post-shot analysis `DRIP_DELAY_S` (3 s) after completion
- If measured weight error ≤ `MAX_OFFSET` (5 g) from goal: learns offset for next shot, saves to EEPROM
- Larger errors are rejected as outliers

### Hardware Pin Configuration

Defined in shot_stopper.h and main.cpp:

```cpp
#define BUTTON_READ_PIN 13       // Button input, active low (34 on ESP32-S3; REED_IN 25 if REEDSWITCH)
#define PRESS_BUTTON_PIN 22      // Button output (opto-coupled to machine)
#define PRESSURE_PIN 32          // ADC input, MPX5500: P = (V - 0.4) * 16 / (3.3 - 0.4) bar
const int DIMMER_PIN = 5;        // Triac gate / opto drive (phase-angle control), main.cpp
#define ZERO_CROSS_PIN 4         // Mains zero-cross detector input, pump_dimmer.h
// Encoder: GPIO 23 (A), GPIO 25 (B), half-quad mode
// Display SPI (reserved, not yet driven): CLK 18, MOSI 19, MISO 21, CS 17, DC 16, RST 14, BL 2
```

`BUTTON_INPUT_PIN` (shot_stopper.cpp) is the active input: `REED_IN` when `REEDSWITCH`, else `BUTTON_READ_PIN`.

### Web Server (webserver.h)

WiFi connects with a 15 s boot timeout; the firmware runs fine without it. REST endpoints:

- `GET /` — dashboard page (dashboard.h, PROGMEM)
- `GET /state` — single JSON with everything the dashboard polls: brewing, scale status, weight, pressure, PID terms, profile, pump PWM
- `GET /start_shot`, `/stop_shot` — presses the machine button via controlTask
- `GET /reset_shot` — ends the shot on ESP/scale only, machine untouched
- `GET /set_pid?kp=&ki=&kd=` — live PID tuning (each gain optional)
- `GET /set_goal_weight?value=` (10–200 g, persisted to EEPROM)
- `GET /set_weight_offset?value=` (0–5 g, persisted to EEPROM)
- `GET /set_pressure_profile?times=T1,T2&pressures=P1,P2` — positive T = from start, negative T = time-left override (persisted to EEPROM)
- `GET /start_cleaning`, `/continue_cleaning`, `/stop_cleaning` — automated backflush cycle (cleaning_cycle.cpp); `GET /set_cleaning?max_pressure=&cycles=&hold_s=&pause_s=&soak_s=` — cleaning parameters (each optional, range-checked, persisted to EEPROM)
- `GET /set_wifi?ssid=&pass=` — store WiFi credentials to EEPROM, used on next boot (boot falls back to the compile-time secrets.h credentials if the stored ones fail; empty ssid reverts to secrets.h); `GET /reboot` — restart via loop() so the response flushes first, refused (409) while brewing/cleaning
- `GET /shots` — history list (newest first), `GET /shot?id=N` — downsampled trajectory
- `GET /api/system/status`, `/api/shots/latest`, `/api/shots/{id}` — Gaggiuino-compatible API so Beanconqueror imports shots post-brew (add ESP IP as a `GAGGIUINO` preparation device; datapoint values are ints ×10; flow derived from weight, temperature zero-filled; `/api/shots/latest` must stay registered before the `/api/shots/*` wildcard; shot JSON MUST include nested `profile.name` — BQ's import modal silently drops shots without it)

Dashboard features: live tiles, start/stop/reset, PID sliders, goal weight/offset editors, pressure profile editor, WiFi credential editor + reboot button, live Chart.js plots, shot history comparison. Chart.js loads from CDN, so the *client browser* needs internet; the ESP does not.

## Configuration and Customization

### Build Settings (platformio.ini)

```ini
[env:upesy_wroom]
platform = espressif32@6.13.0
board = upesy_wroom
framework = arduino
build_flags = -std=c++17
```

### Feature Toggles (shot_stopper.h)

```cpp
#define MOMENTARY true         // GS3-style momentary vs Linea Mini latching switch
#define REEDSWITCH false       // Reed switch detection instead of button input
#define AUTOTARE true          // Auto-tare scale at shot start
#define MIN_SHOT_DURATION_S 3  // Ignore flushes shorter than this
#define MAX_SHOT_DURATION_S 50 // Safety timeout to prevent stuck pump
#define TREND_LINE_DATAPOINTS 10 // Regression window (accuracy vs latency)
```

Debug output is controlled per category in debug.h (`DEBUG_ENABLED`, `DEBUG_SHOT`, `DEBUG_SCALE`, ...).

### EEPROM Storage (settings.h/.cpp)

All persistence goes through one versioned `PersistentSettings` blob (magic `"ESPR"` + version, `EEPROM.put` at address 4, 512-byte EEPROM region) holding goal weight, weight offset, the pressure profile, the full `CleaningConfig`, and optional WiFi credentials. `settingsLoad()` (called first thing in `setup()`, before tasks and WiFi) validates/sanitizes the blob and applies it to `shot`/`cleaningConfig`; `settingsSave()` snapshots the live state back and commits (mutex-serialized — it's called from both controlTask offset learning and the AsyncTCP `/set_*` handlers). The pre-blob two-byte layout (goal weight at byte 0, offset ×10 at byte 1) is migrated automatically on first boot; a magic/version mismatch falls back to legacy bytes + compiled-in defaults. Bump `SETTINGS_VERSION` on any struct layout change.

### WiFi Credentials

`src/secrets.h`:
```cpp
#define ssid "your_ssid"
#define password "your_password"
```
This file contains real credentials — keep it out of version control (.gitignore) and share a `secrets.h.example` instead. These compile-time credentials are the first-boot default and the fallback: credentials stored via `/set_wifi` win at boot, but if they fail to connect within the timeout the firmware retries with the secrets.h ones, so a typo'd password can't lock the dashboard out. Since `ssid`/`password` are macros, no other header may use those tokens as parameter names (settings.h uses `newSsid`/`newPass` for exactly this reason).

## BLE Scale Integration (Acaia Lunar)

All scale access is centralized in `scaleTask` (main.cpp). Critical polling pattern — **`newWeightAvailable()` must be called continuously** or the connection goes stale:

```cpp
if (scale.heartbeatRequired()) scale.heartbeat(); // keep BLE alive ~30 s
if (scale.newWeightAvailable()) {
  currentWeight = scale.getWeight();
  scaleNewWeight = true;
}
```

Other tasks never call the scale directly — they set `scaleTareRequest` / `scaleStartSequenceRequest` / `scaleStopTimerRequest` flags. If the scale disconnects mid-shot, controlTask ends the shot without touching the machine button.

## Testing and Debugging

### Serial Output (115200 baud, requires DEBUG_ENABLED true in debug.h - keep off for real brewing: blocked serial writes throttle the control loop)
- Startup sequence, dashboard URL (re-printed every 10 s)
- Encoder position and PWM values, PID terms
- Button press/release transitions
- Shot lifecycle events (start/stop, end reason)
- Weight/time/pressure readings
- EEPROM offset learning events

### Bench Testing Without Hardware
- `TESTING_MODE_NO_SCALE true` (main.cpp) fakes a connected scale so the dashboard start button works without BLE.

### Header Guard Gotcha
webserver.h intentionally uses `ESPRESSO_WEBSERVER_H` as its guard — `WEBSERVER_H` would make ESPAsyncWebServer think the core WebServer library is present and skip defining `HTTP_GET` etc.

## Hardware and PCB Status

- PCB in `pcb/` directory (KiCad), routing complete with opto-coupler footprints, power connections, on/off switch, display interface
- **Status**: On hold — waiting to afford the next PCB print run
- Pump dimming uses zero-cross-synced phase-angle control (pump_dimmer.cpp); the detector output must be wired to GPIO 4

## Dependencies (platformio.ini lib_deps)

| Library | Purpose |
|---------|---------|
| tatemazer/AcaiaArduinoBLE | BLE scale communication (Acaia Lunar) |
| arduino-libraries/ArduinoBLE @^1.4.0 | BLE stack |
| bblanchon/ArduinoJson @^7.4.1 | JSON serialization for web API |
| madhephaestus/ESP32Encoder @^0.11.8 | Quadrature encoder |
| me-no-dev/AsyncTCP @^1.1.1 | Async TCP for web server |
| me-no-dev/ESPAsyncWebServer (git) | Async web server |

Pump dimming is implemented in-repo (pump_dimmer.cpp) with a GPIO interrupt + ESP32 hardware timer — no dimmer library.

## Important Design Patterns

1. **Task isolation**: BLE, control loop, and HTTP live in separate FreeRTOS tasks; a missing scale or busy web client can never stall brewing or pump control
2. **Flag-based messaging**: `volatile` request flags instead of locks for cross-task commands; only shot history uses a semaphore
3. **State machine buttons**: debounced, machine-agnostic logic handles momentary and latching switches
4. **Auto-learning calibration**: offset improves after each shot; outliers rejected
5. **Predictive shot stopping**: linear regression enables anticipatory (not reactive) stopping
6. **Pressure profiles + PID**: per-shot pressure curves, live-tunable gains
7. **Graceful degradation**: boots and brews without WiFi, without scale (pump stays at 100 %), and reconnects both in the background