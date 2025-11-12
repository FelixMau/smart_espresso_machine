# Display Integration Analysis for Smart Espresso Machine

**Branch**: experimental (display integration study)
**Date**: 2025-11-12
**Purpose**: Evaluate display integration feasibility and identify pin conflicts

---

## Current Pin Usage Summary

### Active Pins (upesy_wroom configuration)

| GPIO | Current Use | Type | Notes |
|------|------------|------|-------|
| **4** | ZERO_CROSS_PIN (unused) | Digital Input | Defined in on_hold.h but not actively used |
| **5** | DIMMER_PIN | PWM Output | Pump speed control (50Hz, 8-bit) |
| **13** | IN (button input) | Digital Input | Active LOW (momentary or latching brew switch) |
| **22** | OUT (button output) | Digital Output | Opto-isolated solenoid control |
| **23** | ENCODER_A | Quadrature Input | Pump power adjustment (encoder half-quad) |
| **25** | ENCODER_B | Quadrature Input | Pump power adjustment (encoder half-quad) |
| **33** | PRESSURE_PIN | Analog Input (ADC1_5) | MPX5500 pressure sensor (0-16 bar) |

### Reserved/Unavailable Pins

| GPIO | Reason | Notes |
|------|--------|-------|
| **0** | Boot mode select | Pulled to ground at boot; avoid for user I/O |
| **1, 3** | UART0 (Serial) | Serial console (9600 baud) - used for debugging |
| **6-11** | SPI flash** | Wired to on-board flash memory |
| **12, 15** | Boot conflicts | MTDI/MTDO can interfere with boot |
| **34-39** | Input-only ADC | Can't be used as digital outputs |

---

## Available GPIO for Display

### Candidate Pins (Unrestricted)

These pins are free and have full I/O capability:

- **GPIO 2** - Fully available (no conflicts)
- **GPIO 8, 9, 10** - Available (SPI flash not used if SD card unused)
- **GPIO 14, 16-20, 21, 24, 26-32** - Mostly available
- **GPIO 35, 36, 39** - Input-only (ADC), not suitable for output

### Not Recommended (Marginal)

- **GPIO 12**: MTDI can cause boot issues if pulled HIGH; prefer to avoid
- **GPIO 15**: MTDO output at boot; can interfere; recommend avoiding
- **GPIO 34-39**: Input-only; can't drive display outputs

---

## Display Integration Options

### Option 1: **SPI Display (RECOMMENDED)** 📡

**Example**: ILI9341 2.8" TFT, SSD1306 128x64 OLED, or GC9A01 circular display

#### Pin Requirements:
- **MOSI** (Master Out Slave In) → GPIO 23 or any available GPIO
- **MISO** (Master In Slave Out) → GPIO 19 (ADC2_3) or available GPIO
- **CLK** (Clock) → GPIO 18 or available GPIO
- **CS** (Chip Select) → GPIO 17 (dedicated SPI)
- **DC** (Data/Command) → GPIO 16 or available
- **RST** (Reset) → GPIO 14 or available
- **BL** (Backlight) → GPIO 2 (PWM-capable) or 32

#### Viability Assessment: ✅ **EXCELLENT**

**Conflicts**: NONE - All SPI pins available
**Current Encoder Impact**: NONE - Can use different pins than GPIO 23/25
**Advantages**:
- Fast communication (typically 10-40 MHz)
- Multiple display libraries available (TFT_eSPI, Adafruit_GFX, LVGL)
- Clean separation: data pins don't interfere with encoder/button logic
- Easy to add backlight PWM control

**Recommended Configuration**:
```cpp
#define MOSI_PIN   19   // GPIO 19 (available)
#define MISO_PIN   21   // GPIO 21 (available)
#define CLK_PIN    18   // GPIO 18 (available)
#define CS_PIN     17   // GPIO 17 (available)
#define DC_PIN     16   // GPIO 16 (available)
#define RST_PIN    14   // GPIO 14 (available)
#define BL_PIN     2    // GPIO 2 (PWM-capable for brightness)
```

---

### Option 2: **I2C Display** 📊

**Example**: SSD1306 128x64 OLED (I2C variant), or other I2C displays

#### Pin Requirements:
- **SDA** (Serial Data) → GPIO 21 (standard for I2C)
- **SCL** (Serial Clock) → GPIO 22 (standard for I2C)
- **VCC, GND** (Power)

#### Viability Assessment: ⚠️ **PROBLEMATIC**

**Conflicts**:
- **GPIO 22 is already used for OUT (button output/solenoid control)**
- Would require re-routing solenoid output to different pin

**Workaround**:
- Move `OUT` from GPIO 22 → GPIO 32 (available, outputs-capable)
- Use GPIO 21 & 22 for I2C display
- Requires PCB redesign if you want to keep button output on GPIO 22

**Not Recommended**: Higher effort than SPI, and doesn't provide clear advantage for your use case

---

### Option 3: **Parallel RGB Display** 🖥️

**Example**: 8-bit parallel TFT (ILI9486 with 8-bit parallel interface)

#### Pin Requirements:
- **8 data lines** (D0-D7) → GPIO 0, 2, 4, 5, 18, 19, 21, 22
- **Write signal** (WR) → GPIO 23
- **Read signal** (RD) → GPIO 25
- **CS, RS, RST** → 3 additional pins

#### Viability Assessment: ❌ **NOT VIABLE**

**Conflicts**:
- Encoder pins 23 & 25 would be consumed by display control signals
- Would lose rotary encoder functionality (primary user interaction)
- Requires 11+ GPIO pins total (you only have ~12 free after current use)
- Makes PCB design significantly more complex

**Recommendation**: Avoid this approach - not worth sacrificing encoder control

---

## Recommended Path: SPI Display Integration

### Hardware Design Considerations

1. **Connector on PCB**:
   - Add a 6-pin or 8-pin header for SPI display
   - Include VCC (3.3V), GND, MOSI, MISO, CLK, CS, DC, RST
   - Optional: Dedicated PWM pin for backlight brightness

2. **Display Library Stack**:
   - **TFT_eSPI**: Simple 2D graphics, optimized for SPI
   - **Adafruit_GFX**: More feature-rich, slower
   - **LVGL**: Full UI framework (more RAM/flash intensive)

3. **Backlight Control**:
   - Use GPIO 2 with PWM for brightness adjustment
   - Allows dimming during idle/brewing phases

4. **Power Budget**:
   - Most small SPI displays: 50-150 mA @ 3.3V
   - ESP32: ~100 mA @ 3.3V
   - Total: ~250 mA (ensure your PSU supports this)

### Firmware Integration Plan

```cpp
// Add to main.cpp includes
#include <TFT_eSPI.h>  // or your chosen library

// Add pin definitions
#define TFT_MOSI   19
#define TFT_MISO   21
#define TFT_SCLK   18
#define TFT_CS     17
#define TFT_DC     16
#define TFT_RST    14
#define TFT_BL     2

// Initialize display in setup()
TFT_eSPI tft = TFT_eSPI(240, 320);  // Width x Height
tft.init();
tft.setRotation(1);

// Update display in loop()
tft.fillScreen(TFT_BLACK);
tft.setCursor(0, 0);
tft.setTextColor(TFT_WHITE);
tft.printf("Weight: %.1f g\n", currentWeight);
tft.printf("Pressure: %.1f bar\n", shot.pressure);
tft.printf("Time: %.1f s\n", shot.shotTimer);
```

---

## Current PCB Viability Check

### What the PCB Currently Supports

Based on the pin definitions in your code:

✅ **Perfect for SPI display**:
- All encoder pins (23, 25) remain free for encoder use
- All button pins (13, 22) remain free for button logic
- Pressure sensor (33) unaffected
- Dimmer (5) unaffected
- Multiple GPIO available for SPI display

⚠️ **I2C would require modification**:
- GPIO 22 conflict would need PCB rerouting
- More effort than SPI solution

❌ **Parallel display is not viable**:
- Would lose encoder functionality
- Not recommended

### PCB Redesign Recommendation

When revising the PCB:

1. **Keep current design as-is** (encoder, button, dimmer, pressure)
2. **Add SPI header** for future display option:
   - 8-pin connector: VCC, GND, MOSI(19), MISO(21), CLK(18), CS(17), DC(16), RST(14)
   - Low cost addition (no complexity)
   - Allows optional display without compromising current features

3. **Optional: Add backlight PWM**:
   - GPIO 2 breakout or dedicated PWM pin for brightness control
   - ~15 more mA power draw if enabled

4. **Optional: Add a reset capacitor** on display RST line for stability

---

## Summary & Recommendation

| Criteria | SPI | I2C | Parallel |
|----------|-----|-----|----------|
| **Viability** | ✅ Excellent | ⚠️ Requires rework | ❌ Not viable |
| **Encoder Impact** | ✅ None | ✅ None | ❌ Breaks encoder |
| **PCB Effort** | Minimal | Medium | High |
| **Display Options** | Abundant | Limited | Abundant |
| **Performance** | Fast | Slow | Very fast |
| **Recommendation** | **CHOOSE THIS** | Not recommended | Avoid |

### Implementation Roadmap

1. **Phase 1** (Firmware, current branch):
   - Add SPI display support with TFT_eSPI
   - Create display abstraction layer (display.h)
   - Implement real-time shot metrics display

2. **Phase 2** (PCB redesign):
   - Add 8-pin SPI header on next PCB revision
   - Include backlight PWM option
   - Document connector pinout clearly

3. **Phase 3** (Optional):
   - Add touchscreen support if desired (uses same SPI pins + IRQ)
   - Implement LVGL UI framework for richer interface

---

## Next Steps

1. **Choose display size & interface** (recommend: 2.4"-2.8" ILI9341 SPI TFT)
2. **Add TFT_eSPI library** to platformio.ini
3. **Create display.h driver** with display initialization & update functions
4. **Integrate with shot.h** data structures for real-time rendering
5. **Test on breadboard** before PCB integration

Would you like me to create an example display driver implementation?
