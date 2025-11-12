/*
 * display_example.h
 *
 * Example SPI display driver for Smart Espresso Machine
 * Demonstrates integration with shot.h data structures
 *
 * Recommended display: ILI9341 2.8" SPI TFT (320x240 or 240x320)
 * or: GC9A01 round 1.28" AMOLED (240x240)
 *
 * PIN CONFIGURATION (All GPIO available, no conflicts with encoder/button):
 *   GPIO 18: TFT_SCLK (SPI Clock)
 *   GPIO 19: TFT_MOSI (SPI Data Out to display)
 *   GPIO 21: TFT_MISO (SPI Data In from display)
 *   GPIO 17: TFT_CS   (Chip Select)
 *   GPIO 16: TFT_DC   (Data/Command select)
 *   GPIO 14: TFT_RST  (Reset)
 *   GPIO 2:  TFT_BL   (Backlight PWM, optional)
 *
 * DEPENDENCIES:
 *   - TFT_eSPI library (add to platformio.ini)
 *   - shot_stopper.h (for Shot structure)
 *
 * INSTALLATION:
 *   1. Add to platformio.ini:
 *      bodmer/TFT_eSPI@^1.5.36
 *
 *   2. Configure TFT_eSPI via user_setup.h or #defines before include
 *
 * USAGE:
 *   #include "display_example.h"
 *
 *   void setup() {
 *     displayInit();
 *   }
 *
 *   void loop() {
 *     displayUpdate(&shot, currentWeight);
 *     delay(200);  // Update display every 200ms
 *   }
 */

#ifndef DISPLAY_EXAMPLE_H
#define DISPLAY_EXAMPLE_H

#include <Arduino.h>

// ============================================================================
// PIN DEFINITIONS (SPI + Control lines)
// ============================================================================

#define TFT_SCLK  18    // SPI Clock
#define TFT_MOSI  19    // SPI MOSI (Display data in)
#define TFT_MISO  21    // SPI MISO (Display data out)
#define TFT_CS    17    // Chip Select
#define TFT_DC    16    // Data/Command
#define TFT_RST   14    // Reset
#define TFT_BL    2     // Backlight PWM (GPIO 2 with PWM support)

// PWM Configuration for backlight
#define BL_CHANNEL 1    // PWM channel (0 used by encoder PWM, use 1)
#define BL_FREQ 1000    // 1 kHz backlight frequency
#define BL_RES 8        // 8-bit resolution (0-255)

// Color definitions (16-bit RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_ORANGE  0xFC20
#define COLOR_GRAY    0x8410

// Display refresh timing
#define DISPLAY_UPDATE_MS 200   // Update display every 200ms
unsigned long lastDisplayUpdate = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

extern struct Shot shot;
extern float currentWeight;

// ============================================================================
// DISPLAY INITIALIZATION
// ============================================================================

/**
 * Initialize display hardware and configure SPI interface
 * Call once in setup()
 */
void displayInit() {
  // Configure SPI pins as outputs
  pinMode(TFT_SCLK, OUTPUT);
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);

  // Configure backlight PWM
  ledcSetup(BL_CHANNEL, BL_FREQ, BL_RES);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, 255);  // Full brightness initially

  Serial.println("Display pins configured");
  // TODO: Initialize TFT_eSPI here
  // tft.init();
  // tft.setRotation(1);
  // tft.fillScreen(COLOR_BLACK);
}

// ============================================================================
// DISPLAY UPDATE FUNCTIONS
// ============================================================================

/**
 * Update display with current shot and weight data
 * Safe to call frequently; respects DISPLAY_UPDATE_MS timing
 */
void displayUpdate(Shot* shot_ptr, float weight) {
  unsigned long now = millis();

  // Throttle updates to reduce flicker and processing load
  if (now - lastDisplayUpdate < DISPLAY_UPDATE_MS) {
    return;
  }
  lastDisplayUpdate = now;

  // TODO: Implement actual display update using TFT_eSPI
  // Layout suggestion:
  // ┌─────────────────────────────┐
  // │  SMART ESPRESSO MACHINE     │
  // ├─────────────────────────────┤
  // │  Weight:  XX.X g  ▁▂▃▅▆█   │
  // │  Goal:    36.0 g  [████████]│
  // │                              │
  // │  Pressure: XX.X bar          │
  // │  Time:     XX.X s            │
  // │  Status:   [BREWING]         │
  // └─────────────────────────────┘

  displayDrawHeader();
  displayDrawWeightBar(weight, shot_ptr->goalWeight);
  displayDrawPressure(shot_ptr->pressure);
  displayDrawTime(shot_ptr->shotTimer);
  displayDrawStatus(shot_ptr->brewing, shot_ptr->end);
}

/**
 * Draw header and title
 */
void displayDrawHeader() {
  // TODO: Implement using TFT_eSPI
  // tft.fillScreen(COLOR_BLACK);
  // tft.setCursor(0, 0);
  // tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  // tft.setTextSize(2);
  // tft.println("ESPRESSO SHOT");
}

/**
 * Draw weight bar with current vs goal weight
 */
void displayDrawWeightBar(float current, float goal) {
  // TODO: Implement bar chart
  // Shows visual progress towards goal weight
  // Example: ▁▂▃▅▆█ progress indicator
}

/**
 * Draw pressure reading
 */
void displayDrawPressure(float pressure) {
  // TODO: Display pressure
  // tft.setCursor(0, 80);
  // tft.printf("Pressure: %.1f bar", pressure);
}

/**
 * Draw shot timer
 */
void displayDrawTime(float time_s) {
  // TODO: Display elapsed time
  // tft.setCursor(0, 100);
  // tft.printf("Time: %.1f s", time_s);
}

/**
 * Draw brewing status indicator
 */
void displayDrawStatus(bool brewing, ENDTYPE end_reason) {
  // TODO: Display status with color coding
  // Brewing: GREEN
  // Complete: WHITE + reason (weight, time, button)
  // Example:
  // tft.setCursor(0, 120);
  // if (brewing) {
  //   tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
  //   tft.println("STATUS: BREWING");
  // } else {
  //   tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  //   switch(end_reason) {
  //     case ENDTYPE::WEIGHT: tft.println("STATUS: GOAL WEIGHT"); break;
  //     case ENDTYPE::TIME: tft.println("STATUS: TIME LIMIT"); break;
  //     case ENDTYPE::BUTTON: tft.println("STATUS: USER STOPPED"); break;
  //     default: tft.println("STATUS: IDLE"); break;
  //   }
  // }
}

/**
 * Set backlight brightness (0-255)
 * Use for idle dimming or power saving
 */
void displaySetBrightness(uint8_t brightness) {
  ledcWrite(BL_CHANNEL, brightness);
  if (brightness == 0) {
    Serial.println("Display backlight OFF");
  } else {
    Serial.printf("Display brightness: %d%%\n", (brightness * 100) / 255);
  }
}

/**
 * Show idle screen (low power mode)
 */
void displayShowIdleScreen() {
  // TODO: Simple idle display
  // Dimmed background, time, next shot info
}

/**
 * Show brewing screen (active shot)
 */
void displayShowBrewingScreen(Shot* shot_ptr, float weight) {
  // TODO: Full real-time metrics display
  // High refresh rate, live graphs
}

/**
 * Show shot summary screen (after brewing ends)
 */
void displayShowSummaryScreen(Shot* shot_ptr, float final_weight) {
  // TODO: Display shot results
  // Final weight, time, pressure curve, error info
}

#endif  // DISPLAY_EXAMPLE_H
