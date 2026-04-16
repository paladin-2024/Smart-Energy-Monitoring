/**
 * =============================================================================
 * FILE:        sketch_wifi.ino
 * PROJECT:     Smart Energy Monitoring System — Part B (ThingSpeak Cloud)
 * DESCRIPTION: Extends Part A with WiFi connectivity. Reads simulated voltage
 *              and current via potentiometers, computes power, drives the OLED
 *              display and status indicators, and uploads sensor data to a
 *              ThingSpeak channel via HTTP POST every 15 seconds.
 *              Includes graceful WiFi reconnect logic so the device recovers
 *              automatically if the network drops.
 * AUTHOR:      Caleb
 * DATE:        2026-04-17
 * VERSION:     1.0.0
 * LICENSE:     MIT
 *
 * HARDWARE (same as Part A):
 *   - ESP32 WROOM-32
 *   - OLED SSD1306 128×64 I2C (SDA=GPIO21, SCL=GPIO22)
 *   - Potentiometer 1 → GPIO34 (voltage, 0–240V)
 *   - Potentiometer 2 → GPIO35 (current, 0–10A)
 *   - Green LED → GPIO26 (SAFE)
 *   - Red LED   → GPIO27 (OVERLOAD)
 *   - Active Buzzer → GPIO25
 *
 * THINGSPEAK CHANNEL FIELDS:
 *   Field 1 → Voltage (V)
 *   Field 2 → Current (A)
 *   Field 3 → Power (W)       ← used for gauge, line graph, MATLAB, alert
 *   Field 4 → Alert flag (0/1)
 *
 * CONFIGURATION:
 *   Update WIFI_SSID, WIFI_PASSWORD, and THINGSPEAK_API_KEY below.
 *
 * LIBRARIES REQUIRED:
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   (WiFi and HTTPClient are built into the ESP32 Arduino core)
 * =============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ---------------------------------------------------------------------------
// USER CONFIGURATION — update these before flashing
// ---------------------------------------------------------------------------

// WiFi credentials — replace with your network name and password
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ThingSpeak Write API Key — found in your channel → API Keys tab
// Example: "ABCDEF1234567890"
#define THINGSPEAK_API_KEY "YOUR_THINGSPEAK_WRITE_API_KEY"

// ThingSpeak HTTP endpoint — do not change this
#define THINGSPEAK_URL "http://api.thingspeak.com/update"

// How often to send data to ThingSpeak (milliseconds)
// ThingSpeak free tier enforces a minimum of 15 seconds between updates.
// Sending faster than this results in a "0" response code (update rejected).
#define UPLOAD_INTERVAL 15000UL   // 15 seconds

// ---------------------------------------------------------------------------
// DISPLAY CONFIGURATION (identical to Part A)
// ---------------------------------------------------------------------------
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64
#define OLED_RESET       -1
#define SCREEN_ADDRESS  0x3C

// ---------------------------------------------------------------------------
// PIN DEFINITIONS (identical to Part A)
// ADC1 pins are used (GPIO34, GPIO35) because ADC2 is disabled when WiFi
// is active — this is the key reason we chose ADC1 pins in the circuit design.
// ---------------------------------------------------------------------------
#define PIN_VOLTAGE    34
#define PIN_CURRENT    35
#define PIN_LED_GREEN  26
#define PIN_LED_RED    27
#define PIN_BUZZER     25

// ---------------------------------------------------------------------------
// SCALING CONSTANTS (identical to Part A)
// ---------------------------------------------------------------------------
#define ADC_MAX         4095.0f
#define MAX_VOLTAGE      240.0f
#define MAX_CURRENT       10.0f
#define POWER_THRESHOLD  150.0f
#define MAX_POWER        300.0f
#define FLASH_INTERVAL   500       // ms between OVERLOAD text flash toggles

// ---------------------------------------------------------------------------
// WiFi RECONNECT SETTINGS
// If WiFi drops, we attempt reconnection up to WIFI_MAX_RETRIES times,
// waiting WIFI_RETRY_DELAY ms between attempts, before giving up for that
// cycle and trying again next loop iteration.
// ---------------------------------------------------------------------------
#define WIFI_MAX_RETRIES   10     // Maximum connection attempts per reconnect
#define WIFI_RETRY_DELAY  500     // ms to wait between each attempt

// ---------------------------------------------------------------------------
// GLOBAL STATE
// ---------------------------------------------------------------------------
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float         g_voltage    = 0.0f;
float         g_current    = 0.0f;
float         g_power      = 0.0f;
int           g_alert      = 0;
bool          g_flash      = false;
unsigned long g_lastFlash  = 0;
unsigned long g_lastUpload = 0;    // Timestamp of last ThingSpeak POST


// ===========================================================================
// WiFi FUNCTIONS
// ===========================================================================

/**
 * connectWiFi()
 *
 * Attempts to connect to the configured WiFi network. Retries up to
 * WIFI_MAX_RETRIES times, printing a dot to Serial on each attempt.
 * Prints the assigned IP address on success, or a failure message if the
 * network cannot be reached within the retry limit.
 *
 * Called from setup() and from checkWiFiReconnect() if connection is lost.
 */
void connectWiFi() {
  // Begin connection attempt with the configured credentials
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("Connecting to WiFi '"));
  Serial.print(WIFI_SSID);
  Serial.print(F("'"));

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_MAX_RETRIES) {
    delay(WIFI_RETRY_DELAY);
    Serial.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Connection successful — log IP address for debugging
    Serial.println();
    Serial.print(F("WiFi connected. IP address: "));
    Serial.println(WiFi.localIP());
  } else {
    // Could not connect — device will retry on next checkWiFiReconnect() call
    Serial.println();
    Serial.println(F("WiFi connection failed. Will retry in next cycle."));
  }
}


/**
 * checkWiFiReconnect()
 *
 * Checks whether the WiFi connection is still active each loop iteration.
 * If the connection has dropped (WL_CONNECTED is false), it cleanly
 * disconnects and attempts to reconnect via connectWiFi().
 *
 * Why graceful disconnect before reconnect?
 *   Calling WiFi.disconnect() before WiFi.begin() clears the previous
 *   connection state, preventing rare cases where the ESP32 gets stuck in
 *   a partial connection state after an unexpected network drop.
 */
void checkWiFiReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi dropped. Attempting reconnect..."));
    WiFi.disconnect();
    delay(100); // Brief pause to let the radio reset cleanly
    connectWiFi();
  }
}


// ===========================================================================
// THINGSPEAK UPLOAD
// ===========================================================================

/**
 * postToThingSpeak()
 *
 * Sends the four sensor fields to the ThingSpeak HTTP API via a GET request
 * with query parameters. ThingSpeak accepts both GET and POST; GET is used
 * here because it is simpler to construct and log for debugging.
 *
 * ThingSpeak field mapping:
 *   field1 = voltage  (V)   — displayed as Numeric widget
 *   field2 = current  (A)   — logged for reference
 *   field3 = power    (W)   — used for Line Graph, Gauge, MATLAB, React alert
 *   field4 = alert  (0/1)   — used to detect overload events in React widget
 *
 * @param v      Voltage in Volts
 * @param i      Current in Amps
 * @param p      Power in Watts
 * @param alert  Alert flag: 0 = SAFE, 1 = OVERLOAD
 */
void postToThingSpeak(float v, float i, float p, int alert) {
  // Skip upload if WiFi is not connected — avoids HTTP timeout stall
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("ThingSpeak: skipped (no WiFi)"));
    return;
  }

  HTTPClient http;

  // Build URL with all four field values as query parameters
  // String concatenation is used for readability; in production a char buffer
  // would be more memory-efficient.
  String url = String(THINGSPEAK_URL)
    + "?api_key=" + THINGSPEAK_API_KEY
    + "&field1="  + String(v, 2)
    + "&field2="  + String(i, 2)
    + "&field3="  + String(p, 2)
    + "&field4="  + String(alert);

  Serial.print(F("ThingSpeak POST → "));
  Serial.println(url);

  // Initialise HTTP client and send GET request
  http.begin(url);
  int httpResponseCode = http.GET();

  // ThingSpeak returns the entry ID (positive integer) on success,
  // or 0 if the update was rejected (e.g. sent too soon — < 15s interval).
  if (httpResponseCode > 0) {
    Serial.print(F("ThingSpeak response: "));
    Serial.println(httpResponseCode); // Entry ID if > 0
  } else {
    // Negative response code means HTTP-level failure (network error, timeout)
    Serial.print(F("ThingSpeak error: "));
    Serial.println(httpResponseCode);
  }

  // Always free the HTTP client resources to prevent memory leaks
  http.end();
}


// ===========================================================================
// SENSOR + DISPLAY FUNCTIONS (identical to Part A)
// ===========================================================================

/**
 * readSensors()
 * Reads both ADC channels and scales to voltage (V), current (A), power (W).
 * Sets g_alert = 1 when power exceeds POWER_THRESHOLD.
 */
void readSensors() {
  int rawVoltage = analogRead(PIN_VOLTAGE);
  int rawCurrent = analogRead(PIN_CURRENT);
  g_voltage = (rawVoltage / ADC_MAX) * MAX_VOLTAGE;
  g_current = (rawCurrent / ADC_MAX) * MAX_CURRENT;
  g_power   = g_voltage * g_current;
  g_alert   = (g_power > POWER_THRESHOLD) ? 1 : 0;
}


/**
 * updateIndicators()
 * Drives green LED (SAFE) or red LED + buzzer (OVERLOAD) based on g_alert.
 */
void updateIndicators() {
  if (g_alert) {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED,   HIGH);
    digitalWrite(PIN_BUZZER,    HIGH);
  } else {
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED,   LOW);
    digitalWrite(PIN_BUZZER,    LOW);
  }
}


/**
 * drawBarGraph()
 * Draws a horizontal power bar at the bottom of the OLED (0–MAX_POWER scale).
 * @param powerVal  Current power in Watts
 */
void drawBarGraph(float powerVal) {
  const int barX = 14, barY = 54, barWidth = 96, barHeight = 7;
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  int fillWidth = (int)((powerVal / MAX_POWER) * (float)(barWidth - 2));
  if (fillWidth < 0) fillWidth = 0;
  if (fillWidth > barWidth - 2) fillWidth = barWidth - 2;
  if (fillWidth > 0)
    display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,   barY); display.print(F("0"));
  display.setCursor(112, barY); display.print(F("3C"));
}


/**
 * updateDisplay()
 * Redraws all OLED elements: title, metrics row, large power, status, bar graph.
 * On OVERLOAD: status text flashes (inverted) every FLASH_INTERVAL ms.
 */
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title + separator
  display.setTextSize(1);
  display.setCursor(4, 0);
  display.print(F("SMART ENERGY MONITOR"));
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Metrics row
  display.setCursor(0,  12); display.print(F("V:")); display.print(g_voltage, 1);
  display.setCursor(44, 12); display.print(F("I:")); display.print(g_current, 2);
  display.setCursor(90, 12); display.print(F("P:")); display.print((int)g_power);

  // Large power value (centred, textSize 2)
  display.setTextSize(2);
  String powerStr = String(g_power, 1) + "W";
  int16_t x1, y1; uint16_t strW, strH;
  display.getTextBounds(powerStr, 0, 0, &x1, &y1, &strW, &strH);
  display.setCursor((SCREEN_WIDTH - strW) / 2, 23);
  display.print(powerStr);

  // Status
  display.setTextSize(1);
  if (g_alert) {
    if (millis() - g_lastFlash >= FLASH_INTERVAL) {
      g_flash = !g_flash;
      g_lastFlash = millis();
    }
    if (g_flash) {
      display.fillRect(10, 42, 108, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(12, 43); display.print(F("!! OVERLOAD !!"));
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(12, 43); display.print(F("!! OVERLOAD !!"));
    }
  } else {
    display.setCursor(20, 43); display.print(F("STATUS: SAFE"));
  }

  drawBarGraph(g_power);
  display.display();
}


/**
 * sendSerial()
 * Outputs structured sensor data line for debugging and serial monitoring.
 * Format: voltage:xxx.xx,current:x.xx,power:xxx.xx,alert:0or1
 */
void sendSerial() {
  Serial.print(F("voltage:"));  Serial.print(g_voltage, 2);
  Serial.print(F(",current:")); Serial.print(g_current, 2);
  Serial.print(F(",power:"));   Serial.print(g_power,   2);
  Serial.print(F(",alert:"));   Serial.println(g_alert);
}


// ===========================================================================
// SETUP + LOOP
// ===========================================================================

/**
 * setup()
 * Initialises serial, GPIO pins, OLED display, and WiFi connection.
 * Shows splash screen then transitions to live display.
 */
void setup() {
  Serial.begin(115200);
  Serial.println(F("Smart Energy Monitor v1.0 — Part B (WiFi + ThingSpeak)"));

  // Configure output pins
  pinMode(PIN_LED_GREEN, OUTPUT); digitalWrite(PIN_LED_GREEN, LOW);
  pinMode(PIN_LED_RED,   OUTPUT); digitalWrite(PIN_LED_RED,   LOW);
  pinMode(PIN_BUZZER,    OUTPUT); digitalWrite(PIN_BUZZER,    LOW);

  // Initialise OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("ERROR: SSD1306 not found."));
    while (true) { delay(1000); }
  }

  // Splash screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 10);  display.println(F("SMART ENERGY MONITOR"));
  display.setCursor(20, 25); display.println(F("Connecting WiFi..."));
  display.display();

  // Connect to WiFi — this may take a few seconds
  connectWiFi();

  // Update splash to show WiFi status
  display.clearDisplay();
  display.setCursor(4, 10); display.println(F("SMART ENERGY MONITOR"));
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(10, 28); display.println(F("WiFi: Connected"));
    display.setCursor(10, 40); display.println(WiFi.localIP().toString());
  } else {
    display.setCursor(10, 28); display.println(F("WiFi: FAILED"));
    display.setCursor(4, 40);  display.println(F("Will retry in loop"));
  }
  display.display();
  delay(2000);

  Serial.println(F("Setup complete. Entering main loop."));
}


/**
 * loop()
 * Main execution loop — runs continuously.
 *
 * Each iteration:
 *   1. Reads sensors and computes power
 *   2. Updates LEDs and buzzer
 *   3. Redraws OLED
 *   4. Outputs serial data
 *   5. Checks WiFi health and reconnects if needed
 *   6. If 15 seconds have elapsed since last upload, POSTs to ThingSpeak
 *   7. Waits 500ms
 *
 * The millis()-based upload timer means the 15-second ThingSpeak interval
 * is respected without blocking the rest of the loop — the display and
 * indicators continue to update every 500ms regardless of upload timing.
 */
void loop() {
  readSensors();
  updateIndicators();
  updateDisplay();
  sendSerial();

  // Check WiFi health every loop — reconnect if dropped
  checkWiFiReconnect();

  // Upload to ThingSpeak every UPLOAD_INTERVAL ms (15 seconds)
  // millis() overflow after ~49 days is handled correctly by unsigned subtraction
  if (millis() - g_lastUpload >= UPLOAD_INTERVAL) {
    postToThingSpeak(g_voltage, g_current, g_power, g_alert);
    g_lastUpload = millis();
  }

  delay(500);
}
