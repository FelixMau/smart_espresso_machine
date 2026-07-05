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

# Monitor serial output (9600 baud)
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
- **PID-controlled pump dimmer** (PWM, 50 Hz) following configurable pressure profiles
- **Rotary encoder** for manual pump power adjustment
- **Async web dashboard** for live monitoring, control, and PID tuning over LAN
- **EEPROM storage** for goal weight and learned calibration offsets

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
| `debug.h` | ~70 | Category-based serial debug macros (`DEBUG_SHOT_PRINT`, `DEBUG_SCALE_PRINT`, ...) |
| `secrets.h` | 1 | WiFi credentials (`ssid`, `password`) — must NOT be committed. Include AFTER the WiFi headers: the macros clobber their parameter names otherwise |

### FreeRTOS Task Structure (main.cpp)

- **controlTask** (core 1, priority 2, ~50 ms cadence): scale-data consumption, shot trajectory updates, PID pump control, button state machine, shot end detection, EEPROM learning. Never blocks on BLE or HTTP.
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

**PID Pressure Control** (pid_controller.cpp + main.cpp:controlIteration)
- During a shot, pump PWM = `pressurePID.calculate(shot.currentGoalPressure, shot.pressure)`
- Default gains Kp=25, Ki=0.5, Kd=8; live-tunable via web (`/set_pid`)
- Output floor prevents pump shutoff; anti-windup on integral term
- Idle state: pump at 100 % (PWM 255)

**Pressure Profiles** (shot_stopper.cpp:updateShotTrajectory)
- Two goal lists, up to `MAX_PRESSURE_GOALS` (8) each:
  - **By time**: seconds from shot start (default: 2 bar @ 0 s → 9 bar @ 5 s → 6 bar @ 20 s)
  - **By time-left**: overrides based on predicted remaining time (default: 4 bar for last 5 s)
- Set via web as comma lists; negative times encode "time left"

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
const int DIMMER_PIN = 5;        // Pump dimmer PWM (50 Hz, 8-bit, LEDC channel 0), main.cpp
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
- `GET /set_pressure_profile?times=T1,T2&pressures=P1,P2` — positive T = from start, negative T = time-left override
- `GET /shots` — history list (newest first), `GET /shot?id=N` — downsampled trajectory
- `GET /api/system/status`, `/api/shots/latest`, `/api/shots/{id}` — Gaggiuino-compatible API so Beanconqueror imports shots post-brew (add ESP IP as a `GAGGIUINO` preparation device; datapoint values are ints ×10; flow derived from weight, temperature zero-filled; `/api/shots/latest` must stay registered before the `/api/shots/*` wildcard; shot JSON MUST include nested `profile.name` — BQ's import modal silently drops shots without it)

Dashboard features: live tiles, start/stop/reset, PID sliders, goal weight/offset editors, pressure profile editor, live Chart.js plots, shot history comparison. Chart.js loads from CDN, so the *client browser* needs internet; the ESP does not.

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

### EEPROM Storage

- **Address 0** (`WEIGHT_ADDR`): Goal weight (uint8_t; validated to 10–200 g on boot, default 36 g)
- **Address 1** (`OFFSET_ADDR`): Weight offset × 10 (0–25.5 g; validated ≤ MAX_OFFSET, default 1.5 g)

### WiFi Credentials

`src/secrets.h`:
```cpp
#define ssid "your_ssid"
#define password "your_password"
```
This file contains real credentials — keep it out of version control (.gitignore) and share a `secrets.h.example` instead.

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

### Serial Output (9600 baud)
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
- **Known limitation**: PWM dimmer lacks zero-crossing detection (currently basic 50 Hz PWM)

## Dependencies (platformio.ini lib_deps)

| Library | Purpose |
|---------|---------|
| tatemazer/AcaiaArduinoBLE | BLE scale communication (Acaia Lunar) |
| arduino-libraries/ArduinoBLE @^1.4.0 | BLE stack |
| bblanchon/ArduinoJson @^7.4.1 | JSON serialization for web API |
| madhephaestus/ESP32Encoder @^0.11.8 | Quadrature encoder |
| me-no-dev/AsyncTCP @^1.1.1 | Async TCP for web server |
| me-no-dev/ESPAsyncWebServer (git) | Async web server |

Pump dimming uses the ESP32 LEDC peripheral directly (no dimmer library).

## Important Design Patterns

1. **Task isolation**: BLE, control loop, and HTTP live in separate FreeRTOS tasks; a missing scale or busy web client can never stall brewing or pump control
2. **Flag-based messaging**: `volatile` request flags instead of locks for cross-task commands; only shot history uses a semaphore
3. **State machine buttons**: debounced, machine-agnostic logic handles momentary and latching switches
4. **Auto-learning calibration**: offset improves after each shot; outliers rejected
5. **Predictive shot stopping**: linear regression enables anticipatory (not reactive) stopping
6. **Pressure profiles + PID**: per-shot pressure curves, live-tunable gains
7. **Graceful degradation**: boots and brews without WiFi, without scale (pump stays at 100 %), and reconnects both in the background