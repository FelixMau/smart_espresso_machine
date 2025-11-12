# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

**Smart Espresso Machine** is an embedded IoT retrofit that automates espresso shot brewing on existing machines using:
- **ESP32 microcontroller** with custom PCB integration
- **Bluetooth scale** (Acaia Lunar) for weight measurement via BLE
- **Pressure sensor** (MPX5500, 0-16 bar) for extraction monitoring
- **Opto-isolated button control** to automate pump and solenoid
- **Rotary encoder** for manual pump power adjustment during brewing
- **PWM dimmer control** (50 Hz) for pump speed regulation
- **EEPROM storage** for learned calibration offsets

The system uses **linear regression** to predict when shots reach target weight and automatically stops extraction at the optimal moment.

## Architecture

### Production Code Structure

**`src/main.cpp`** - Current entry point (37 lines)
- Implements **encoder-to-PWM control** for manual pump power adjustment
- Added due to PCB hardware failure requiring quick workaround
- Full scale integration, button automation, and web monitoring are on hold

**`test/on_hold.h`** - Previous complete implementation (111 lines)
- **Full-featured version** with BLE scale, button logic, shot automation, and web server
- Superseded by current main.cpp encoder workaround due to hardware issues
- Contains the integration pattern needed to restore full automation
- Can be referenced when fixing the PCB and restoring automation features

**`test/*.h`** - Production-ready modular libraries
- `shot_stopper.h` (~400 lines) - Core brewing logic and automation
- `webserver.h` (~186 lines) - Async REST API and web monitoring
- `SuperMon.h` (~114 lines) - Embedded HTML/CSS/JS dashboard

### Core Data Structure: `struct Shot` (shot_stopper.h)

Central state object tracking:
- Brew timing (`shotTimer`, `expected_end_s`)
- Weight measurements (`weight[]`, `time_s[]`, `datapoints`)
- Pressure readings and control goals
- User parameters (`goalWeight`, `weightOffset`)
- Brewing status flags and end reason (`ENDTYPE::WEIGHT`, `ENDTYPE::TIME`, etc.)

### Key Algorithms

**Linear Regression Prediction** (shot_stopper.h:calculateEndTime)
- Fits line to last N=10 weight-vs-time datapoints
- Predicts: `expected_end_s = (goalWeight - weightOffset - b) / m`
- Only calculates after 10+ measurements to avoid erratic predictions
- Enables stopping shots before over-extraction

**Button State Machine** (shot_stopper.h:handleButtonLogic)
- Four states: IDLE → PRESSED → [HELD] → RELEASED
- Debouncing via majority voting (8-31 sample array, 40-155ms window)
- Supports two machine types:
  - **Momentary switches** (GS3 AV): Press toggles brew on/off
  - **Latching switches** (Linea Mini): Press starts, system controls via output pin after 3s
- Handles reed switch alternative input

**EEPROM Auto-Learning** (shot_stopper.h:detectShotError)
- Post-shot analysis 3 seconds after completion
- If measured weight error < 5g from goal: learns offset for next shot
- Saves offset to EEPROM address 1
- Improves accuracy over multiple shots; rejects outliers

**Pressure Profile Control** (shot_stopper.h)
- Two modes: time-based (from shot start) and time-remaining (countdown)
- Up to 8 pressure goals per mode
- Interpolated control between goals

### Hardware Pin Configuration (shot_stopper.h)

```cpp
#define IN 13              // Button input (active low)
#define OUT 22             // Button output (opto-coupled to machine)
#define PRESSURE_PIN 33    // ADC input for pressure sensor
                           // Conversion: ADC → 0-16 bar
                           // Formula: (V - 0.4) * (16 / 2.9)
```

Encoder pins: GPIO 23 (A), GPIO 25 (B)
Dimmer PWM: GPIO 5 (50 Hz, 8-bit resolution)

**Alternative ESP32S3 pins available via `#ifdef` directives**

### Web Server Features (webserver.h)

REST endpoints for monitoring and control:
- `GET /` - Serve SuperMon.h dashboard
- `GET /weight`, `/pressure`, `/shottime` - Real-time data
- `GET /set_goal_weight?value=XX` - Update target weight
- `GET /set_pressure_profile?times=T1,T2&pressures=P1,P2` - Configure pressure goals

Web dashboard provides:
- Start/Stop shot buttons
- Weight offset slider (0-5g) for manual calibration
- Real-time data table with live updates (200ms refresh)

## Configuration and Customization

### Build Settings (platformio.ini)

```ini
[env:upesy_wroom]
platform = espressif32
board = upesy_wroom
framework = arduino
build_flags = -std=c++17
```

Supports feature toggling via `#define` in shot_stopper.h:

```cpp
#define MOMENTARY true        // Machine button type (GS3 momentary vs Linea Mini latching)
#define REEDSWITCH false      // Use reed switch detection instead of button input
#define AUTOTARE true         // Auto-tare scale at shot start
#define MIN_SHOT_DURATION_S 3 // Reject shots shorter than 3 seconds (prevent accidental triggers)
#define MAX_SHOT_DURATION_S 50 // Safety timeout to prevent stuck pump
#define N 10                  // Datapoints for linear regression (accuracy vs latency)
```

### EEPROM Storage

- **Address 0**: Goal weight (uint8_t, 0-255g)
- **Address 1**: Weight offset × 10 (stores 0-25.5g, recall with `/10`)

Clear EEPROM by writing 255 to both addresses.

### WiFi (Optional)

Create `test/secrets.h` for WiFi credentials:
```cpp
#define LOCAL_SSID "your_ssid"
#define LOCAL_PASS "your_password"
```

## Integration Path

### Current State (Temporary Workaround)
- **src/main.cpp**: Encoder-to-PWM control only (37 lines)
  - Added as quick fix due to PCB hardware failure
  - Allows manual pump control while automation is suspended
  - Missing: BLE scale, button automation, shot monitoring, web server

### Previous Full Implementation
- **test/on_hold.h**: Complete, working integration (111 lines)
  - Demonstrates correct setup() initialization pattern
  - Shows loop() structure for scale polling, button handling, and shot monitoring
  - Includes WiFi/web server integration
  - Reference this when restoring full automation after PCB fix

### To Restore Full Automation (Post-PCB Fix)
1. Reference the setup() pattern from on_hold.h:
   - CPU frequency, serial, EEPROM initialization
   - GPIO pins (button input, output, dimmer)
   - BLE initialization for scale connection
   - WiFi and web server startup
2. Reference the loop() pattern from on_hold.h:
   - Scale connection check with fallback (pump ON)
   - Scale heartbeat and weight polling
   - Shot trajectory updates and button handling
   - Shot end detection and error learning
3. **Integrate encoder control** from current main.cpp:
   - Encoder initialization in setup()
   - Read encoder position in loop()
   - Map encoder position to PWM output
4. Test full integration on hardware:
   - Verify scale BLE connection stability
   - Test button automation (momentary vs latching)
   - Validate pressure readings and profiles
   - Confirm EEPROM learning and persistence

## BLE Scale Integration (Acaia Lunar)

Critical polling pattern - **must call `newWeightAvailable()` continuously**:

```cpp
if (scale.heartbeatRequired()) {
  scale.heartbeat();  // Keep BLE alive every ~30s
}
if (scale.newWeightAvailable()) {
  weight = scale.getWeight();  // Fetch latest
  // Without continuous polling, data goes stale
}
```

Missing `newWeightAvailable()` calls in loop → scale connection drops.

## Testing and Debugging

### Serial Output (9600 baud)
- Encoder position and PWM values
- Button press/release transitions
- Shot lifecycle events (start/stop, end reason)
- Weight/time/pressure readings
- EEPROM offset learning events

### Compilation Variants
Code uses `#ifdef` directives for:
- Board variants (ESP32S3 vs generic ESP32)
- Machine types (momentary vs latching buttons, reed switch alternative)
- Optional features (pressure control, web interface)

### Unit Tests
```bash
platformio test
```
PlatformIO framework supports native unit tests.

## Hardware and PCB Status

- **Nearly production-ready PCB** in `pcb/` directory (KiCad 6.0)
- **Status**: Routing complete, awaiting second opinion before manufacturing
- **Features**: Proper opto-coupler footprints, power connections, on/off switch
- **Known limitation**: PWM dimmer lacks zero-crossing detection (currently basic 50Hz PWM)

Recent improvements:
- Fixed opto-coupler footprints
- Added on/off switch for power management
- Corrected routing issues from initial prototype

## Dependencies

| Library | Purpose | Notes |
|---------|---------|-------|
| AcaiaArduinoBLE | BLE scale communication | Custom library for Acaia Lunar |
| ArduinoBLE | BLE peripheral | v1.4.0+ |
| ArduinoJson | JSON serialization | v7.4.1+ for web API |
| rbdimmerESP32 | Dimmer control | Custom, zero-crossing ready |
| ESP32Encoder | Quadrature encoder | v0.11.8+ |
| ArduinoEEPROM | Persistent storage | Standard library |
| WiFi.h | WiFi connectivity | Standard (optional) |
| AsyncTCP/ESPAsyncWebServer | Web server | Standard (optional) |

## Pressure Sensor

**Sensor**: MPX5500, 0-16 bar absolute pressure

**Conversion formula** (shot_stopper.h:updatePressureSensor):
```
Voltage = (ADC_value / 4095) * 3.3V
Pressure = (Voltage - 0.4V) * (16 bar / 2.9V)
```

Readings available in real-time during brewing and stored in Shot.pressure.

## Important Design Patterns

1. **Modular headers**: Core logic in .h files allows incremental testing and development without full firmware compilation
2. **State machine buttons**: Debounced, machine-agnostic logic handles both momentary and latching switches
3. **Auto-learning calibration**: Offset improves after each shot; rejected outliers prevent regression errors
4. **Predictive shot stopping**: Linear regression enables anticipatory stopping (not reactive)
5. **Pressure profiles**: Flexible system allows per-shot pressure curves for different brewing techniques
6. **EEPROM persistence**: Learned offsets and goal weights survive power cycles
