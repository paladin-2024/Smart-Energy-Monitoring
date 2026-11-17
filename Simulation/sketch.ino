/**
 * =============================================================================
 * FILE:        sketch.ino
 * PROJECT:     Smart Energy Monitoring System — Part A (Wokwi Simulation)
 * DESCRIPTION: ESP32 firmware that reads simulated voltage and current sensors
 *              via potentiometers, computes real-time power (P = V × I),
 *              displays readings on an SSD1306 OLED, drives status LEDs and
 *              a buzzer, and outputs structured serial data for cloud ingestion.
 * AUTHOR:      Caleb
 * DATE:        2026-04-17
 * VERSION:     1.0.0
 * LICENSE:     MIT
 *
 * HARDWARE:
 *   - ESP32 WROOM-32 (microcontroller)
 *   - OLED SSD1306 128×64 I2C (display)
 *   - Potentiometer 1 on GPIO34 — simulates ACS712 current sensor (0–10A)
 *   - Potentiometer 2 on GPIO35 — simulates voltage divider (0–240V)
 *   - Green LED on GPIO26 — SAFE indicator
 *   - Red LED   on GPIO27 — OVERLOAD indicator
 *   - Active Buzzer on GPIO25 — audible overload alert
 *
 * LIBRARIES REQUIRED (install via Arduino Library Manager):
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *
 * SERIAL OUTPUT FORMAT (115200 baud):
 *   voltage:xxx.xx,current:x.xx,power:xxx.xx,alert:0or1
 * =============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------------------------------------------------------------------
// DISPLAY CONFIGURATION
// SSD1306 128×64 connected via I2C (SDA=GPIO21, SCL=GPIO22 on ESP32 default)
// OLED_RESET = -1 means we share the Arduino reset pin — correct for most
// breakout boards that do not have a dedicated reset pin.
// ---------------------------------------------------------------------------
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1       // -1 = share Arduino reset, no dedicated pin
#define SCREEN_ADDRESS 0x3C      // Default I2C address for 128×64 SSD1306

// ---------------------------------------------------------------------------
// PIN DEFINITIONS
// GPIO34 and GPIO35 are input-only ADC1 pins — ideal for analog reads.
// We avoid ADC2 pins because WiFi (used in Part B) disables ADC2 when active.
// ---------------------------------------------------------------------------
#define PIN_VOLTAGE   34   // ADC1_CH6 — potentiometer 1 (simulates voltage)
#define PIN_CURRENT   35   // ADC1_CH7 — potentiometer 2 (simulates current)
#define PIN_LED_GREEN 26   // Output — green LED (SAFE state)
#define PIN_LED_RED   27   // Output — red LED (OVERLOAD state)
#define PIN_BUZZER    25   // Output — active buzzer (OVERLOAD alert)

// ---------------------------------------------------------------------------
// SCALING CONSTANTS
// ESP32 ADC is 12-bit → raw range 0–4095.
// We map this linearly to the physical sensor ranges:
//   Voltage: 0–240V  (Uganda mains nominal is 240V)
//   Current: 0–10A   (ACS712-10A sensor range)
// Power threshold of 150W is the assignment-specified overload boundary.
// MAX_POWER sets the 100% mark on the bar graph — 300W gives comfortable
// headroom above the 150W threshold so the bar is never clipped.
// ---------------------------------------------------------------------------
#define ADC_MAX        4095.0f   // 12-bit ADC maximum raw value
#define MAX_VOLTAGE    240.0f    // Maximum simulated voltage in Volts
#define MAX_CURRENT     10.0f   // Maximum simulated current in Amps
#define POWER_THRESHOLD 150.0f  // Overload threshold in Watts
#define MAX_POWER      300.0f   // Full-scale value for the bar graph (Watts)

// ---------------------------------------------------------------------------
// DISPLAY FLASH TIMING
// On OVERLOAD the status text alternates between normal and inverted every
// FLASH_INTERVAL milliseconds — creates a visible pulsing alert on the OLED.
// ---------------------------------------------------------------------------
#define FLASH_INTERVAL 500       // Milliseconds between flash state toggles

// ---------------------------------------------------------------------------
// GLOBAL STATE
// ---------------------------------------------------------------------------
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float    g_voltage   = 0.0f;   // Scaled voltage reading (V)
float    g_current   = 0.0f;   // Scaled current reading (A)
float    g_power     = 0.0f;   // Computed power (W)
int      g_alert     = 0;      // Alert flag: 0 = SAFE, 1 = OVERLOAD
bool     g_flash     = false;  // Current flash state for OVERLOAD animation
unsigned long g_lastFlash = 0; // Timestamp of last flash toggle (ms)


// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
/**
 * Initialises serial communication, GPIO pins, and the OLED display.
 * Shows a 1.5-second splash screen so the user knows the device has booted.
 *
 * Called once by the Arduino runtime before loop() begins.
 */
void setup() {
  // Start serial at 115200 baud — used for debug output and cloud data feed
  Serial.begin(115200);
  Serial.println(F("Smart Energy Monitor v1.0 — booting..."));

  // Configure indicator output pins
  // LOW = off at startup; indicators are driven by updateIndicators()
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED,   OUTPUT);
  pinMode(PIN_BUZZER,    OUTPUT);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED,   LOW);
  digitalWrite(PIN_BUZZER,    LOW);

  // Initialise I2C OLED display
  // begin() returns false if the display is not found on the I2C bus — halt
  // rather than proceed silently, because a missing display would make the
  // device look broken with no feedback to the user.
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("ERROR: SSD1306 not found. Check wiring (SDA=21, SCL=22)."));
    while (true) { delay(1000); } // Halt — nothing more we can do
  }

  // Splash screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 18);
  display.println(F("SMART ENERGY MONITOR"));
  display.setCursor(28, 34);
  display.println(F("Initialising..."));
  display.display();
  delay(1500);

  Serial.println(F("Display OK. Starting main loop."));
}


// ---------------------------------------------------------------------------
// readSensors()
// ---------------------------------------------------------------------------
/**
 * Reads both potentiometers via ADC and scales the raw 12-bit values to
 * physical voltage (V), current (A), power (W), and alert flag.
 *
 * Scaling formula:
 *   physical = (raw / ADC_MAX) * PHYSICAL_MAX
 *
 * Why potentiometers instead of real sensors?
 *   In the Wokwi simulation we cannot simulate analogue sensor ICs, so a
 *   potentiometer wiper voltage stands in for the ACS712 output and voltage
 *   divider output respectively. The scaling math is identical to what a
 *   real sensor interface would produce.
 *
 * Updates global: g_voltage, g_current, g_power, g_alert
 */
void readSensors() {
  // Read raw ADC values (0–4095)
  int rawVoltage = analogRead(PIN_VOLTAGE);
  int rawCurrent = analogRead(PIN_CURRENT);

  // Scale to physical units
  g_voltage = (rawVoltage / ADC_MAX) * MAX_VOLTAGE;
  g_current = (rawCurrent / ADC_MAX) * MAX_CURRENT;

  // Compute instantaneous power using Ohm's law analogue: P = V × I
  g_power = g_voltage * g_current;

  // Set alert flag — used by LED/buzzer logic and serial output
  g_alert = (g_power > POWER_THRESHOLD) ? 1 : 0;
}


// ---------------------------------------------------------------------------
// updateIndicators()
// ---------------------------------------------------------------------------
/**
 * Drives the green LED, red LED, and buzzer based on the current alert state.
 *
 * SAFE (g_alert == 0):
 *   Green LED ON, Red LED OFF, Buzzer OFF
 *
 * OVERLOAD (g_alert == 1):
 *   Green LED OFF, Red LED ON, Buzzer ON (continuous tone)
 *
 * The active buzzer used in Wokwi triggers on HIGH — no PWM needed.
 */
void updateIndicators() {
  if (g_alert) {
    // OVERLOAD — engage red alert chain
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED,   HIGH);
    digitalWrite(PIN_BUZZER,    HIGH);
  } else {
    // SAFE — disengage all alerts, enable green indicator
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED,   LOW);
    digitalWrite(PIN_BUZZER,    LOW);
  }
}


// ---------------------------------------------------------------------------
// drawBarGraph()
// ---------------------------------------------------------------------------
/**
 * Renders a horizontal power bar graph at the bottom of the OLED (y=54–62).
 * The fill width is proportional to g_power relative to MAX_POWER.
 * Scale tick labels "0" and "300W" are drawn at each end.
 *
 * Why a bar graph?
 *   It gives an instant at-a-glance sense of how close the system is to the
 *   overload threshold — more intuitive than a number alone.
 *
 * @param powerVal  Current power reading in Watts (used to compute fill %)
 */
void drawBarGraph(float powerVal) {
  const int barX      = 14;   // Left edge of bar (after "0" label)
  const int barY      = 54;   // Top edge of bar
  const int barWidth  = 96;   // Total width in pixels
  const int barHeight =  7;   // Height in pixels

  // Outer border
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);

  // Inner fill — clamp to bar interior to avoid overwriting the border
  int fillWidth = (int)((powerVal / MAX_POWER) * (float)(barWidth - 2));
  if (fillWidth < 0) fillWidth = 0;
  if (fillWidth > barWidth - 2) fillWidth = barWidth - 2;

  if (fillWidth > 0) {
    display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
  }

  // Scale labels at each end
  display.setTextSize(1);
  display.setCursor(0, barY);
  display.print(F("0"));
  display.setCursor(112, barY);
  display.print(F("3C")); // "3C" shorthand for 300W — fits in 2 chars at textSize 1
}


// ---------------------------------------------------------------------------
// updateDisplay()
// ---------------------------------------------------------------------------
/**
 * Clears the OLED and redraws all display elements for the current frame:
 *   Row 0  : "SMART ENERGY MONITOR" title + separator line
 *   Row 1  : V / I / P metrics on one row (textSize 1)
 *   Row 2  : Power value large and centred (textSize 2)
 *   Row 3  : Status text — "STATUS: SAFE" or flashing "!! OVERLOAD !!"
 *   Bottom : Horizontal bar graph (0–300W scale)
 *
 * The OVERLOAD status flashes by alternating between normal and inverted
 * text every FLASH_INTERVAL ms — uses millis() so loop() is never blocked.
 *
 * Note: SSD1306 is monochrome. There is no colour change on overload —
 *       the visual cue is the flashing inverted text block.
 */
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ── Title ──────────────────────────────────────────────────────────────
  display.setTextSize(1);
  display.setCursor(4, 0);
  display.print(F("SMART ENERGY MONITOR"));
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE); // Separator

  // ── Metrics row: V  I  P ──────────────────────────────────────────────
  // Three values squeezed onto one row at textSize 1 (6px per char)
  display.setTextSize(1);

  display.setCursor(0, 12);
  display.print(F("V:"));
  display.print(g_voltage, 1);

  display.setCursor(44, 12);
  display.print(F("I:"));
  display.print(g_current, 2);

  display.setCursor(90, 12);
  display.print(F("P:"));
  display.print((int)g_power);

  // ── Power value (large, centred) ──────────────────────────────────────
  display.setTextSize(2); // textSize 2 = 12×16 px per char — visible from distance
  String powerStr = String(g_power, 1) + "W";

  // Centre the string horizontally
  int16_t x1, y1;
  uint16_t strW, strH;
  display.getTextBounds(powerStr, 0, 0, &x1, &y1, &strW, &strH);
  display.setCursor((SCREEN_WIDTH - strW) / 2, 23);
  display.print(powerStr);

  // ── Status text ────────────────────────────────────────────────────────
  display.setTextSize(1);

  if (g_alert) {
    // Flash logic — toggle state every FLASH_INTERVAL ms without blocking
    if (millis() - g_lastFlash >= FLASH_INTERVAL) {
      g_flash     = !g_flash;
      g_lastFlash = millis();
    }

    if (g_flash) {
      // Inverted block: fill white rectangle, draw black text on top
      // This creates a strong visual pulse effect on the monochrome display
      display.fillRect(10, 42, 108, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(12, 43);
      display.print(F("!! OVERLOAD !!"));
      display.setTextColor(SSD1306_WHITE); // Restore for subsequent draws
    } else {
      display.setCursor(12, 43);
      display.print(F("!! OVERLOAD !!"));
    }
  } else {
    display.setCursor(20, 43);
    display.print(F("STATUS: SAFE"));
  }

  // ── Bar graph ──────────────────────────────────────────────────────────
  drawBarGraph(g_power);

  // Push buffer to physical display — single atomic refresh avoids flicker
  display.display();
}


// ---------------------------------------------------------------------------
// sendSerial()
// ---------------------------------------------------------------------------
/**
 * Outputs a single line of structured sensor data to the Serial port.
 * Format is consumed by Part B (sketch_wifi.ino) for cloud upload parsing
 * and can be monitored via the Arduino Serial Monitor at 115200 baud.
 *
 * Output format:
 *   voltage:xxx.xx,current:x.xx,power:xxx.xx,alert:0or1
 *
 * Example:
 *   voltage:220.45,current:0.68,power:149.91,alert:0
 *   voltage:230.00,current:0.87,power:200.10,alert:1
 */
void sendSerial() {
  Serial.print(F("voltage:"));  Serial.print(g_voltage, 2);
  Serial.print(F(",current:")); Serial.print(g_current, 2);
  Serial.print(F(",power:"));   Serial.print(g_power,   2);
  Serial.print(F(",alert:"));   Serial.println(g_alert);
}


// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
/**
 * Main execution loop — runs continuously after setup().
 * Each iteration:
 *   1. Reads both ADC channels and computes power
 *   2. Updates LED and buzzer outputs
 *   3. Redraws the OLED display
 *   4. Sends structured serial output
 *   5. Waits 500ms before the next cycle
 *
 * The 500ms delay keeps the display smooth and the serial output readable
 * without overwhelming the Wokwi simulator or the serial monitor.
 */
void loop() {
  readSensors();
  updateIndicators();
  updateDisplay();
  sendSerial();
  delay(500); // 2 updates/second — smooth for display, readable for serial
}
