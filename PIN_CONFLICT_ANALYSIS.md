# Pin Conflict Analysis - Complete Verification

**Date**: 2025-11-12
**Branch**: experimental
**Purpose**: Verify display pins don't conflict with pump control, webserver, WiFi, or BLE

---

## ✅ VERIFIED: Current Pin Usage in Codebase

### Hardware Control Pins (CRITICAL - DO NOT CONFLICT)

| Pin | Name | Current Use | Type | Risk | Notes |
|-----|------|-------------|------|------|-------|
| **5** | DIMMER_PIN | Pump speed PWM | PWM Out | 🟢 SAFE | Encoder-controlled pump dimmer (50Hz, 8-bit) |
| **22** | OUT | Solenoid control | Digital Out | 🟢 SAFE | Button output (opto-isolated solenoid) |
| **13** | IN | Button input | Digital In | 🟢 SAFE | Brew switch / Reed switch input |
| **23** | ENCODER_A | Encoder input | Digital In | 🟢 SAFE | Quadrature encoder (pump control, half-quad) |
| **25** | ENCODER_B | Encoder input | Digital In | 🟢 SAFE | Quadrature encoder (pump control, half-quad) |
| **33** | PRESSURE_PIN | Pressure sensor | Analog In | 🟢 SAFE | MPX5500 pressure sensor (ADC1_5) |

### System/Communication Pins (RESERVED BY FRAMEWORK)

| Pin | Use | Type | Notes |
|-----|-----|------|-------|
| **0, 1, 3** | Serial/Boot | Reserved | UART0 (serial console), boot select |
| **6-11** | SPI Flash | Reserved | On-board flash memory (DO NOT USE) |
| **2** | AVAILABLE | GPIO | ✅ Free for display backlight PWM |
| **4** | ZERO_CROSS_PIN | GPIO | Currently unused (defined but not used) |

### WiFi & BLE (ESP32 Built-in, No GPIO Required)

| System | Pins Used | Type | Notes |
|--------|-----------|------|-------|
| **WiFi** | None (internal) | Built-in | Uses internal antenna/transceiver, no GPIO pins |
| **BLE** | None (internal) | Built-in | Uses internal antenna/transceiver, no GPIO pins |
| **AsyncTCP** | None (internal) | Built-in | Software stack only, no GPIO pins |

**IMPORTANT**: WiFi and BLE do NOT reserve any GPIO pins. They use internal ESP32 RF hardware.

---

## 🔍 Pump Control Deep Dive

### Pump Speed Control: GPIO 5 (DIMMER_PIN)

**Current Implementation** (main.cpp:20):
```cpp
const int DIMMER_PIN = 5;      // PWM output pin
const int freq = 50;           // PWM frequency (Hz)
const int pwmChannel = 0;      // PWM channel
const int resolution = 8;      // 8-bit resolution (0 to 255)

// In setup():
ledcSetup(pwmChannel, freq, resolution);
ledcAttachPin(DIMMER_PIN, pwmChannel);

// In loop():
long encoderPosition = encoder.getCount();
int pwmValue = constrain(encoderPosition, 0, 255);
ledcWrite(pwmChannel, pwmValue);
```

**What this means**:
- Uses LEDC (LED Control) hardware PWM module, channel 0
- GPIO 5 is output-only (no input needed)
- Frequency: 50Hz (mains frequency for AC dimmer compatibility)
- Resolution: 8-bit (0-255 PWM values)

**Conflict Risk with Display**: 🟢 **NONE**
- Display uses GPIO 14,16,17,18,19,21 (no conflict with GPIO 5)
- Display doesn't need LEDC channel 0 (LEDC channel 1 available for backlight)
- Pump PWM and display SPI are completely independent

---

### Solenoid Output: GPIO 22 (OUT)

**Current Implementation** (shot_stopper.h:38, 206):
```cpp
#define OUT 22              // Button output (opto-isolated to machine)

// Used in setBrewingState():
digitalWrite(OUT, HIGH);    // Solenoid ON
digitalWrite(OUT, LOW);     // Solenoid OFF
```

**What this means**:
- Simple digital output (HIGH/LOW)
- Opto-isolated (external circuit, doesn't need special pin features)
- Used for brewing state control

**Conflict Risk with Display**: 🟢 **NONE**
- Display uses GPIO 14,16,17,18,19,21 (no conflict with GPIO 22)
- GPIO 22 is simple I/O, not part of SPI or PWM system

---

### Button Input: GPIO 13 (IN)

**Current Implementation** (shot_stopper.h:42, 336):
```cpp
#define IN 13               // Button input (active LOW)

// Used in handleButtonLogic():
int newButtonState = digitalRead(in);  // Read button state
```

**What this means**:
- Simple digital input
- Active LOW (button pulls to ground when pressed)
- Uses internal pull-up (INPUT_PULLUP)

**Conflict Risk with Display**: 🟢 **NONE**
- Display uses GPIO 14,16,17,18,19,21 (no conflict with GPIO 13)

---

### Encoder Inputs: GPIO 23, 25 (ENCODER_A, ENCODER_B)

**Current Implementation** (main.cpp:15-16, 73):
```cpp
const int encoderPinA = 23;    // Encoder pin A
const int encoderPinB = 25;    // Encoder pin B

// In setup():
encoder.attachHalfQuad(encoderPinA, encoderPinB);
encoder.setCount(0);

// In loop():
long encoderPosition = encoder.getCount();
```

**What this means**:
- Uses ESP32Encoder library (quadrature decoder in hardware)
- Half-quad mode (only needs 2 pins instead of 4)
- Dedicated encoder hardware on these pins

**Conflict Risk with Display**: 🟢 **NONE**
- Display uses GPIO 14,16,17,18,19,21 (no conflict with GPIO 23,25)
- Encoder has dedicated hardware decoder (doesn't use SPI/I2C)

---

### Pressure Sensor: GPIO 33 (PRESSURE_PIN)

**Current Implementation** (shot_stopper.h:47, 240):
```cpp
#define PRESSURE_PIN 33     // Analog pin for pressure sensor (MPX5500)

// In updatePressureSensor():
int adcValue = analogRead(PRESSURE_PIN);
float voltage = (adcValue / 4095.0) * 3.3;
float pressure = (voltage - 0.4) * (16.0 / 2.9);
```

**What this means**:
- ADC1 channel 5 (analog input only)
- Single-ended measurement (no special hardware)
- One-time reads in firmware loop

**Conflict Risk with Display**: 🟢 **NONE**
- Display uses GPIO 14,16,17,18,19,21 (GPIO 33 is ADC-only input)
- Pressure sensor doesn't interfere with SPI communication

---

## 📋 WebServer Pin Analysis

### Ports Used by WebServer

**AsyncWebServer**:
- Listens on TCP port 80 (HTTP)
- No GPIO pins required (software stack only)
- No hardware modifications needed

**WiFi Module**:
- Uses internal ESP32 WiFi radio
- No GPIO pins required
- Shares antenna with BLE

**BLE Module**:
- Uses internal ESP32 Bluetooth radio
- No GPIO pins required
- Shares antenna with WiFi

**AsyncTCP**:
- Software TCP/IP stack
- No GPIO pins required
- Built into ESP32 framework

**CRITICAL FINDING**: WebServer/WiFi/BLE use NO GPIO pins!

---

## 🎯 Display Pin Allocation Verification

### Recommended SPI Display Pins (VERIFIED SAFE)

| Display Pin | GPIO # | Current Use | Conflict Risk | Status |
|-------------|--------|-------------|---------------|--------|
| MOSI (SI) | 19 | NONE | 🟢 No | ✅ AVAILABLE |
| MISO (SO) | 21 | NONE | 🟢 No | ✅ AVAILABLE |
| CLK | 18 | NONE | 🟢 No | ✅ AVAILABLE |
| CS | 17 | NONE | 🟢 No | ✅ AVAILABLE |
| DC | 16 | NONE | 🟢 No | ✅ AVAILABLE |
| RST | 14 | NONE | 🟢 No | ✅ AVAILABLE |
| BL (PWM) | 2 | NONE | 🟢 No | ✅ AVAILABLE (PWM ch1) |
| GND | - | GND | 🟢 No | ✅ SHARED |
| VCC | - | 3.3V | 🟢 No | ✅ SHARED |

**ALL DISPLAY PINS ARE SAFE** ✅

---

## 🚨 Potential Conflicts - DETAILED CHECK

### Does Display Interfere with Pump Control?

**DIMMER_PIN (GPIO 5)**:
- Display uses: GPIO 14,16,17,18,19,21,2
- Pump uses: GPIO 5
- Result: ✅ **NO CONFLICT**

**OUT (GPIO 22)**:
- Display uses: GPIO 14,16,17,18,19,21,2
- Solenoid uses: GPIO 22
- Result: ✅ **NO CONFLICT**

### Does Display Interfere with Button Input?

**IN (GPIO 13)**:
- Display uses: GPIO 14,16,17,18,19,21,2
- Button uses: GPIO 13
- Result: ✅ **NO CONFLICT**

### Does Display Interfere with Encoder?

**ENCODER_A (GPIO 23)**, **ENCODER_B (GPIO 25)**:
- Display uses: GPIO 14,16,17,18,19,21,2
- Encoder uses: GPIO 23,25
- Result: ✅ **NO CONFLICT**

### Does Display Interfere with Pressure Sensor?

**PRESSURE_PIN (GPIO 33)**:
- Display uses: GPIO 14,16,17,18,19,21,2 (digital I/O)
- Pressure uses: GPIO 33 (analog input only)
- Result: ✅ **NO CONFLICT**

### Does Display Interfere with WiFi/BLE?

- WiFi uses: No GPIO pins (internal radio)
- BLE uses: No GPIO pins (internal radio)
- Display uses: GPIO 14,16,17,18,19,21,2
- Result: ✅ **NO CONFLICT**

### Does Display Interfere with WebServer?

- WebServer uses: No GPIO pins (software stack)
- Display uses: GPIO 14,16,17,18,19,21,2
- Result: ✅ **NO CONFLICT**

---

## 📊 Complete GPIO Allocation Table

```
GPIO | Current Use           | Display Use | Conflict? | Status
━━━━━┿━━━━━━━━━━━━━━━━━━━━━━┿━━━━━━━━━━━━┿━━━━━━━━━━┿━━━━━━━
  0  | BOOT_SELECT           | None        | -         | RESERVED
  1  | TX0 (Serial)          | None        | -         | RESERVED
  2  | Available             | BL (PWM)    | ✅ No     | DISPLAY
  3  | RX0 (Serial)          | None        | -         | RESERVED
  4  | Available (unused)    | None        | ✅ No     | FREE
  5  | DIMMER_PIN (PWM)      | None        | ✅ No     | PUMP
  6  | SPI Flash             | None        | -         | RESERVED
  7  | SPI Flash             | None        | -         | RESERVED
  8  | Available             | None        | ✅ No     | FREE
  9  | SPI Flash             | None        | -         | RESERVED
 10  | SPI Flash             | None        | -         | RESERVED
 11  | SPI Flash             | None        | -         | RESERVED
 12  | Available             | None        | ✅ No     | FREE (avoid if possible)
 13  | IN (Button)           | None        | ✅ No     | BUTTON
 14  | Available             | RST         | ✅ No     | DISPLAY
 15  | Available             | None        | ✅ No     | FREE (avoid if possible)
 16  | Available             | DC          | ✅ No     | DISPLAY
 17  | Available             | CS          | ✅ No     | DISPLAY
 18  | Available             | CLK         | ✅ No     | DISPLAY
 19  | Available             | MOSI        | ✅ No     | DISPLAY
 20  | Available             | None        | ✅ No     | FREE
 21  | Available             | MISO        | ✅ No     | DISPLAY
 22  | OUT (Solenoid)        | None        | ✅ No     | SOLENOID
 23  | ENCODER_A             | None        | ✅ No     | ENCODER
 24  | Available             | None        | ✅ No     | FREE
 25  | ENCODER_B             | None        | ✅ No     | ENCODER
 26  | Available             | None        | ✅ No     | FREE
 27  | Available             | None        | ✅ No     | FREE
 28  | Available             | None        | ✅ No     | FREE
 29  | Available             | None        | ✅ No     | FREE
 30  | Available             | None        | ✅ No     | FREE
 31  | Available             | None        | ✅ No     | FREE
 32  | Available             | None        | ✅ No     | FREE
 33  | PRESSURE_PIN (ADC)    | None        | ✅ No     | PRESSURE
 34  | INPUT_ONLY (ADC)      | None        | ✅ No     | FREE
 35  | INPUT_ONLY (ADC)      | None        | ✅ No     | FREE
 36  | INPUT_ONLY (ADC)      | None        | ✅ No     | FREE
 37  | INPUT_ONLY (ADC)      | None        | ✅ No     | FREE
 38  | INPUT_ONLY (ADC)      | None        | ✅ No     | FREE
 39  | INPUT_ONLY (ADC)      | None        | ✅ No     | FREE
```

---

## ✅ FINAL VERIFICATION SUMMARY

### Hardware Control (Pump/Solenoid/Button/Encoder)

| Component | GPIO | Type | Display Conflict? | Status |
|-----------|------|------|-------------------|--------|
| Pump Dimmer | 5 | PWM Out | ✅ No | SAFE |
| Solenoid | 22 | Digital Out | ✅ No | SAFE |
| Button | 13 | Digital In | ✅ No | SAFE |
| Encoder A | 23 | Digital In | ✅ No | SAFE |
| Encoder B | 25 | Digital In | ✅ No | SAFE |
| Pressure | 33 | Analog In | ✅ No | SAFE |

### Software/Communication

| System | GPIO Required? | Display Conflict? | Status |
|--------|----------------|-------------------|--------|
| WiFi | No (internal) | ✅ No | SAFE |
| BLE | No (internal) | ✅ No | SAFE |
| WebServer | No (software) | ✅ No | SAFE |
| AsyncTCP | No (software) | ✅ No | SAFE |

### Display Pins

| Display Pin | GPIO | Current Use | Conflict? | Status |
|-------------|------|-------------|-----------|--------|
| MOSI | 19 | None | ✅ No | AVAILABLE |
| MISO | 21 | None | ✅ No | AVAILABLE |
| CLK | 18 | None | ✅ No | AVAILABLE |
| CS | 17 | None | ✅ No | AVAILABLE |
| DC | 16 | None | ✅ No | AVAILABLE |
| RST | 14 | None | ✅ No | AVAILABLE |
| BL | 2 | None | ✅ No | AVAILABLE |

---

## 🎯 CONCLUSION

### ✅ COMPLETELY VERIFIED: Display Integration is SAFE

**Zero conflicts detected** between:
- ✅ Pump speed control (GPIO 5 PWM)
- ✅ Solenoid output (GPIO 22 digital)
- ✅ Button input (GPIO 13 digital)
- ✅ Encoder inputs (GPIO 23, 25 digital)
- ✅ Pressure sensor (GPIO 33 analog)
- ✅ WiFi (no GPIO pins used)
- ✅ BLE (no GPIO pins used)
- ✅ WebServer (no GPIO pins used)
- ✅ Display pins (GPIO 2,14,16,17,18,19,21)

### Recommendation

**PROCEED WITH SPI DISPLAY INTEGRATION** - All checks passed ✅

Use recommended pins:
- GPIO 18: CLK
- GPIO 19: MOSI
- GPIO 21: MISO
- GPIO 17: CS
- GPIO 16: DC
- GPIO 14: RST
- GPIO 2: BL (backlight PWM)

No firmware changes required to pump control, solenoid, button, or encoder logic.

