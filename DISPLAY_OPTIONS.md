# Display Options & Compatibility Guide

This document lists recommended displays that work with the current PCB pin configuration and ESP32 capabilities.

---

## Recommended Displays (SPI - BEST OPTION)

### 1. **ILI9341 2.8" TFT (240x320 or 320x240)**
- **Resolution**: 320×240 pixels
- **Interface**: SPI (4-wire)
- **Cost**: $8-15 USD (AliExpress)
- **Library**: TFT_eSPI (bodmer/TFT_eSPI)
- **Pro**: Bright, fast, good for coffee shop environment, landscape/portrait modes
- **Con**: Larger form factor, moderate power consumption (~80mA)
- **Source**: AliExpress, Adafruit, SparkFun
- **Viability**: ✅ **EXCELLENT** - Recommended choice

```
Pin Connections:
  VCC     → 3.3V
  GND     → GND
  CLK     → GPIO 18
  MOSI    → GPIO 19
  MISO    → GPIO 21 (optional, some versions)
  CS      → GPIO 17
  DC      → GPIO 16
  RST     → GPIO 14
  LED (BL)→ GPIO 2 (PWM for brightness)
```

---

### 2. **GC9A01 1.28" Round AMOLED (240x240)**
- **Resolution**: 240×240 pixels (circular)
- **Interface**: SPI (4-wire)
- **Cost**: $15-25 USD
- **Library**: TFT_eSPI (with GC9A01 support)
- **Pro**: Beautiful AMOLED colors, compact, unique circular form factor
- **Con**: Slightly higher cost, less common libraries, smaller screen area
- **Source**: AliExpress, Aliexpress (AMOLED versions)
- **Viability**: ✅ **GOOD** - Beautiful but more niche

```
Same pin configuration as ILI9341
```

---

### 3. **ST7735 1.44" TFT (128x160)**
- **Resolution**: 128×160 pixels
- **Interface**: SPI (4-wire)
- **Cost**: $3-8 USD
- **Library**: TFT_eSPI (supports ST7735)
- **Pro**: Very cheap, tiny, low power, good for minimalist design
- **Con**: Small screen (limited real estate), lower resolution
- **Source**: AliExpress, Adafruit
- **Viability**: ⚠️ **FAIR** - Good for learning/prototype, but cramped for shot metrics

```
Same pin configuration as above
```

---

### 4. **ILI9486 3.5" TFT (320x480 parallel)**
- **Resolution**: 320×480 pixels
- **Interface**: Parallel 16-bit
- **Cost**: $15-25 USD
- **Library**: TFT_eSPI (parallel mode)
- **Pro**: Larger, higher resolution, excellent for detailed metrics
- **Con**: **NOT VIABLE - Uses GPIO 23 & 25 which conflict with encoder**
- **Viability**: ❌ **NOT RECOMMENDED**

---

### 5. **ST7789 1.3" or 1.54" TFT (240x240 or 240x135)**
- **Resolution**: 240×240 or 240×135 pixels
- **Interface**: SPI (4-wire)
- **Cost**: $5-12 USD
- **Library**: TFT_eSPI (excellent support)
- **Pro**: Balanced size/resolution, responsive, decent screen area
- **Con**: None really - excellent mid-range option
- **Source**: AliExpress, Adafruit, SparkFun
- **Viability**: ✅ **EXCELLENT** - Best value/performance balance

```
Same SPI pin configuration
```

---

### 6. **Waveshare RP2040 Display Modules**
- **Various sizes**: 0.96" OLED to 2.13" e-ink
- **Interface**: SPI (some with I2C)
- **Cost**: $10-20 USD
- **Pro**: Pre-integrated, ready to use, no soldering
- **Con**: Slightly higher cost, e-ink is slow refresh
- **Viability**: ⚠️ **OKAY** - Good if you want plug-and-play

---

## NOT VIABLE (Conflicts with Current Design)

### ❌ I2C Displays (0x3C or 0x3D address)
- **Why**: GPIO 22 already used for solenoid output
- **Examples**: SSD1306 128x64 OLED (I2C), SH1106 OLED
- **Workaround Required**: Move OUT pin from GPIO 22 → GPIO 32 (PCB redesign needed)
- **Recommendation**: Use SPI instead (no rework needed)

### ❌ Parallel 16-bit/8-bit TFT
- **Why**: GPIO 23 & 25 (encoder) would be consumed by data pins
- **Examples**: ILI9486, ILI9488 (parallel mode)
- **Impact**: **Loses rotary encoder control** - Not acceptable
- **Recommendation**: Avoid completely

---

## Display Selection Matrix

| Model | Size | Res | Interface | Cost | Power | Viability | Recommendation |
|-------|------|-----|-----------|------|-------|-----------|-----------------|
| **ILI9341** | 2.8" | 320×240 | SPI | $10 | 80mA | ✅ Best | **TOP CHOICE** |
| **ST7789** | 1.3" | 240×240 | SPI | $7 | 50mA | ✅ Best | **RUNNER UP** |
| **GC9A01** | 1.28" | 240×240 | SPI | $20 | 60mA | ✅ Good | Premium choice |
| **ST7735** | 1.44" | 128×160 | SPI | $5 | 40mA | ⚠️ Fair | Learning/proto |
| **SSD1306 OLED** | 0.96" | 128×64 | I2C | $3 | 20mA | ❌ Conflict | Requires rework |
| **ILI9486 Par** | 3.5" | 320×480 | Parallel | $20 | 120mA | ❌ Broken | **AVOID** |

---

## Recommended Implementation Strategy

### For Current Firmware (No PCB Changes Needed)

1. **Start with ST7789 1.3" SPI TFT**
   - Cheap ($7)
   - Good balance of size and resolution
   - Excellent library support
   - Perfect for iterating firmware

2. **Then upgrade to ILI9341 2.8" SPI TFT**
   - Larger, more visible
   - Better for espresso bar environment
   - Same pins, just swap display
   - Professional appearance

### For Next PCB Revision

1. **Keep SPI header as designed** (pins 18, 19, 21, 17, 16, 14)
2. **Add optional backlight PWM** (GPIO 2 available)
3. **Add reset capacitor** on display RST line
4. **Document pinout clearly** in assembly guide

---

## Library Installation

Add to `platformio.ini` for your chosen display:

```ini
lib_deps =
    # ... existing dependencies ...
    bodmer/TFT_eSPI@^1.5.36        # For ILI9341, ST7789, ST7735
```

Then configure `TFT_eSPI/User_Setup.h` with your pin definitions:

```cpp
#define TFT_MOSI 19   // SPI MOSI
#define TFT_MISO 21   // SPI MISO
#define TFT_SCLK 18   // SPI Clock
#define TFT_CS   17   // Chip select
#define TFT_DC   16   // Data/Command
#define TFT_RST  14   // Reset
#define TFT_BL   2    // Backlight (optional PWM)

// Choose your display
#define ILI9341_DRIVER    // Uncomment for ILI9341
// #define ST7789_DRIVER   // Uncomment for ST7789
```

---

## Power Budget

Typical power consumption:

| Component | Current | Notes |
|-----------|---------|-------|
| ESP32 | 100 mA | Active operation |
| ILI9341 Display | 80 mA | At full brightness |
| Backlight PWM | 0-100 mA | Adjustable |
| Encoder | <5 mA | Low power quadrature |
| Scale BLE | 20 mA | Periodic polling |
| **Total (max)** | **~300 mA** | Ensure PSU supports |

**Recommended**: 5V / 1-2A USB power supply (provides ~300mA @ 3.3V after regulators)

---

## Next Steps

1. **Choose display** from recommended list above
2. **Order from AliExpress/Adafruit** (takes 1-2 weeks for AliExpress)
3. **Add TFT_eSPI to platformio.ini**
4. **Implement display_example.h functions**
5. **Test on breadboard** before final PCB integration

**Recommended starting point**: ST7789 1.3" (cheap, cheerful, perfect for prototyping)

