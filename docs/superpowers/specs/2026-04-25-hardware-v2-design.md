# Smart Energy Monitoring System — Hardware v2 Design Spec

**Date:** 2026-04-25
**Author:** Caleb
**Status:** Approved — ready for implementation

---

## Table of Contents

1. [Overview](#1-overview)
2. [Decisions Made During Brainstorming](#2-decisions-made-during-brainstorming)
3. [Hardware Layer](#3-hardware-layer)
4. [Firmware Structure](#4-firmware-structure)
5. [Cloud Layer](#5-cloud-layer)
6. [AI Layer](#6-ai-layer)
7. [Flutter App](#7-flutter-app)
8. [Repository Structure](#8-repository-structure)
9. [Implementation Order](#9-implementation-order)

---

## 1. Overview

A real-hardware upgrade of the Smart Energy Monitoring System. Replaces the Wokwi ESP32 simulation with a physical ESP8266 NodeMCU V3, real ACS712 current sensor, and a 3.5" TFT SPI 480×320 display. Adds a Flutter mobile app (Android + iOS) with live dashboards, historical charts, AI-generated insights, and Firebase push notifications for overload events.

**Key changes from v1 (simulation):**

| Feature | v1 (Simulation) | v2 (Hardware) |
|---|---|---|
| MCU | ESP32 WROOM-32 | ESP8266 NodeMCU V3 |
| Display | OLED SSD1306 128×64 | 3.5" TFT SPI 480×320 |
| Sensing | 2× potentiometers (simulated) | ACS712 (real current) + fixed 240V |
| Simulation | Wokwi browser sim | Real physical hardware |
| AI | NumPy linear regression | Prophet + Gemini Flash 2.0 |
| Mobile | None | Flutter app (Android + iOS) |
| Notifications | None | Firebase Cloud Messaging (FCM) |

---

## 2. Decisions Made During Brainstorming

| Decision | Choice | Reason |
|---|---|---|
| Voltage sensing | Fixed 240V (Uganda grid) | ESP8266 has only 1 ADC — use it entirely for ACS712 current sensing |
| Current sensing | ACS712 on A0 via 1kΩ pot voltage divider | Real measurement, pot scales 0–5V → 0–3.3V for NodeMCU A0 |
| TFT layout | Split: left panel metrics + right panel live graph | Best use of 480×320 widescreen, polished look |
| Cloud architecture | ThingSpeak HTTP → Lambda webhook | Avoids MQTT/TLS memory pressure on ESP8266 (80KB RAM) |
| Forecasting AI | Prophet (open-source, runs in Lambda) | Purpose-built for time-series, free, no API call |
| Insights AI | Gemini Flash 2.0 | Free tier (1M tokens/day), generates natural language insights |
| Mobile app | Flutter | User is familiar with Dart/Flutter, cross-platform (Android + iOS) |
| Flutter layout | Cards + Chart + AI insight card | Information-dense but not bulky, glanceable |
| Push notifications | Firebase Cloud Messaging (FCM) | Native Flutter integration, free, reliable |
| Potentiometers | 1× 1kΩ pot as voltage divider | User has 2× 1kΩ pots — 1 used as ACS712 voltage divider, 1 spare/backlight |

---

## 3. Hardware Layer

### Components

| Component | Role |
|---|---|
| ESP8266 NodeMCU V3 | Main microcontroller |
| ACS712 current sensor | Measures real AC/DC current |
| 3.5" TFT SPI 480×320 (ILI9488, non-touch) | Live dashboard display |
| 1× 1kΩ potentiometer | Voltage divider for ACS712 → A0 |
| Green LED + 220Ω resistor | SAFE indicator |
| Red LED + 220Ω resistor | OVERLOAD indicator |
| Active buzzer | Audible overload alert |
| Breadboard | Circuit assembly |

### Pin Mapping

| Component | NodeMCU Pin | GPIO | Boot constraint |
|---|---|---|---|
| TFT SCK | D5 | 14 | Safe |
| TFT MOSI | D7 | 13 | Safe |
| TFT CS | D1 | 5 | Safe |
| TFT DC | D2 | 4 | Safe |
| TFT RST | D3 | 0 | HIGH at boot ✓ (RST is active LOW) |
| TFT BL | 3.3V | — | Constant backlight |
| ACS712 OUT | A0 | A0 | Only ADC pin on ESP8266 |
| Green LED | D6 | 12 | Safe |
| Red LED | D8 | 15 | LOW at boot → LED off ✓ |
| Buzzer | D0 | 16 | Safe (output only) |

### ACS712 Voltage Divider

The ACS712 outputs 0–5V. NodeMCU A0 accepts 0–3.3V. A 1kΩ potentiometer wired as a voltage divider scales the signal safely.

```
ACS712 OUT ──── Pin 1 (end of pot)
                      │
                   1kΩ POT
                      │
                 Pin 2 (wiper) ──── A0 (NodeMCU)
                      │
                 Pin 3 (end) ──── GND
```

**Calibration:** Set the wiper to ~66% position (toward GND). With no load on ACS712 (0A), ACS712 outputs 2.5V → A0 should read ~1.65V. Adjust pot until confirmed.

### Current & Power Calculation

```cpp
// Read 20 samples and average (reduces noise)
int sum = 0;
for (int i = 0; i < 20; i++) { sum += analogRead(A0); delay(1); }
float rawADC = sum / 20.0;

float voltage_at_pin = rawADC * (3.3 / 1023.0);   // NodeMCU A0 = 10-bit, 3.3V ref
float acs_output     = voltage_at_pin * (5.0 / 3.3); // undo voltage divider
float current        = (acs_output - 2.5) / 0.185;  // ACS712-5A: 185mV/A
float power          = 240.0 * abs(current);          // fixed 240V Uganda grid
int   alert          = (power > 150.0) ? 1 : 0;
```

> **ACS712 version check:** Confirm which version you have before flashing.
> The label on the module reads "ACS712ELCTR-**05**B", "**20**A", or "**30**A".
> - 5A version  → use `0.185` (sensitivity 185 mV/A)
> - 20A version → use `0.100` (sensitivity 100 mV/A)
> - 30A version → use `0.066` (sensitivity  66 mV/A)

### TFT_eSPI User_Setup.h Configuration

Edit `Arduino/libraries/TFT_eSPI/User_Setup.h`:

```cpp
// 1. Uncomment ONLY this line (comment out all other drivers):
#define ILI9488_DRIVER

// 2. Set pin definitions:
#define TFT_CS    5    // D1
#define TFT_DC    4    // D2
#define TFT_RST   0    // D3
#define TFT_MOSI  13   // D7
#define TFT_SCLK  14   // D5

// 3. Set SPI frequency:
#define SPI_FREQUENCY  27000000
```

---

## 4. Firmware Structure

**File:** `Hardware/sketch_hardware.ino`

**Libraries (Arduino Library Manager):**
- `TFT_eSPI` by Bodmer
- `ArduinoJson` by Benoit Blanchon
- `ESP8266WiFi` (built into ESP8266 Arduino core)
- `ESP8266HTTPClient` (built into ESP8266 Arduino core)

**Sketch structure:**

```
setup()
 ├── Serial.begin(115200)
 ├── TFT init → splash screen ("Smart Energy Monitor" + version)
 ├── WiFi connect → show IP on TFT
 └── pinMode for LEDs + buzzer

loop() — 500ms cycle
 ├── readSensors()
 │     └── 20-sample average on A0 → current → power → alert flag
 ├── updateIndicators()
 │     └── green/red LED + buzzer on 150W threshold
 ├── updateTFT()
 │     ├── LEFT PANEL (40% width, 480px total)
 │     │     ├── "SMART ENERGY" title
 │     │     ├── 240 V  (fixed, white)
 │     │     ├── X.XX A (green)
 │     │     ├── XXX W  (large, green/red based on alert)
 │     │     ├── Status bar (green SAFE / red OVERLOAD)
 │     │     ├── WiFi indicator
 │     │     └── "ThingSpeak: Xs ago"
 │     └── RIGHT PANEL (60% width)
 │           ├── "POWER TREND" label
 │           ├── Scrolling line chart (30 points, shift left on each update)
 │           ├── Red dashed 150W threshold line
 │           └── Y-axis: 0W → 300W
 ├── checkWiFiReconnect()
 └── every 15s → postToThingSpeak(voltage, current, power, alert)
```

**ThingSpeak POST:**
```
GET http://api.thingspeak.com/update?api_key=KEY
    &field1=240        (voltage)
    &field2=<current>  (amps)
    &field3=<power>    (watts)
    &field4=<alert>    (0 or 1)
```

---

## 5. Cloud Layer

### Data Flow

```
ESP8266
  │  HTTP GET every 15s → ThingSpeak
  ↓
ThingSpeak Channel (4 fields: V, I, P, Alert)
  │  ThingHTTP webhook → triggers on every new entry
  ↓
AWS Lambda (Python, triggered via HTTP)
  ├── 1. Parse incoming ThingSpeak payload
  ├── 2. Save to DynamoDB (PK: deviceId, SK: timestamp)
  ├── 3. Query last 50 readings from DynamoDB
  ├── 4. Prophet → predicted_power
  ├── 5. Gemini Flash → insight text
  ├── 6. Anomaly check: |power - mean| > 2σ
  ├── 7. If power > 150W OR anomaly:
  │         → FCM push notification to Flutter app
  └── 8. Lambda URL endpoint → returns last 50 readings as JSON (Grafana)
```

### DynamoDB Schema

```
Table: EnergyReadings
  PK: deviceId  (String) — "esp8266-001"
  SK: timestamp (String) — "2026-04-25T19:32:00Z" (ISO 8601 UTC)
  Attributes:
    voltage         (Number)
    current         (Number)
    power           (Number)
    alert           (Number, 0 or 1)
    predicted_power (Number)
    insight         (String)

Table: FCMTokens
  PK: deviceId  (String) — "esp8266-001"
  Attributes:
    fcm_token   (String) — Firebase device token from Flutter app
    updated_at  (String) — ISO 8601 timestamp of last token refresh
```

### FCM Token Flow

1. Flutter app launches → requests FCM token from Firebase
2. App HTTP POSTs token to Lambda registration endpoint
3. Lambda stores token in DynamoDB (`fcm_tokens` table, keyed by deviceId)
4. When alert fires, Lambda fetches token → calls FCM API → push delivered to phone

---

## 6. AI Layer

### Prophet (Forecasting — runs inside Lambda)

```python
from prophet import Prophet
import pandas as pd

df = pd.DataFrame({'ds': timestamps, 'y': power_values})
model = Prophet(daily_seasonality=True)
model.fit(df)
future = model.make_future_dataframe(periods=1, freq='15S')
forecast = model.predict(future)
predicted_power = float(forecast['yhat'].iloc[-1])
```

Learns daily patterns (morning/evening peaks). Improves accuracy after 2–3 days of real data.

### Gemini Flash 2.0 (Insights — Google AI API)

```python
import google.generativeai as genai

genai.configure(api_key=GEMINI_API_KEY)
model = genai.GenerativeModel('gemini-2.0-flash')

prompt = f"""
You are an energy monitoring assistant in Uganda.
Last 50 power readings (watts, newest last): {power_list}
Current reading: {current_power}W. Predicted next: {predicted_power}W.
Give exactly one short sentence insight about this pattern.
"""
insight = model.generate_content(prompt).text.strip()
```

Free tier: 1,000,000 tokens/day — sufficient for hourly or anomaly-triggered calls.

### Anomaly Detection

```python
import statistics
mean = statistics.mean(power_values)
std  = statistics.stdev(power_values)
is_anomaly = abs(current_power - mean) > (2 * std)
```

---

## 7. Flutter App

### Screens

**HomeScreen (Dashboard)**
- AppBar: "Smart Energy" + live status dot
- 3× MetricCard widgets (Voltage / Current / Power)
- PowerChart: `fl_chart` line chart, last 100 readings, 150W red dashed threshold
- AIInsightCard: Gemini insight text + predicted next value
- StatusPill: animated green SAFE / red OVERLOAD

**HistoryScreen**
- Full-screen `fl_chart` with date range selector
- Fetches ThingSpeak REST API (up to 8000 entries)

**AlertsScreen**
- Scrollable list of past FCM push notification events
- Each item: timestamp + power value + insight text

### Data Sources

| Data | Source | Refresh |
|---|---|---|
| Live readings (V, I, P) | ThingSpeak REST API | Poll every 10s |
| Historical chart | ThingSpeak REST API | On screen open |
| AI insight + prediction | Stored in DynamoDB, fetched via Lambda URL | On new ThingSpeak entry |
| Push notifications | Firebase Cloud Messaging | Real-time, Lambda-triggered |

### Flutter Packages

```yaml
dependencies:
  flutter:
    sdk: flutter
  fl_chart: ^0.68.0
  firebase_core: ^3.0.0
  firebase_messaging: ^15.0.0
  flutter_local_notifications: ^17.0.0
  http: ^1.2.0
  riverpod: ^2.5.0
```

### Push Notification Payload

```json
{
  "title": "⚡ High Power Alert",
  "body": "576W detected — Evening peak 34% above weekly average.",
  "data": {
    "power": "576",
    "predicted": "590",
    "insight": "Evening peak 34% above weekly average.",
    "timestamp": "2026-04-25T19:32:00Z"
  }
}
```

---

## 8. Repository Structure

```
Smart Energy Monitoring System/
├── Hardware/
│   └── sketch_hardware.ino       ← ESP8266 + TFT + ACS712 firmware
├── App/
│   └── smart_energy_app/         ← Flutter project root
│       ├── lib/
│       │   ├── main.dart
│       │   ├── screens/
│       │   │   ├── home_screen.dart
│       │   │   ├── history_screen.dart
│       │   │   └── alerts_screen.dart
│       │   ├── widgets/
│       │   │   ├── metric_card.dart
│       │   │   ├── power_chart.dart
│       │   │   ├── ai_insight_card.dart
│       │   │   └── status_pill.dart
│       │   └── services/
│       │       ├── thingspeak_service.dart
│       │       ├── notification_service.dart
│       │       └── lambda_service.dart
│       └── pubspec.yaml
├── AI&Cloud /
│   ├── lambda_function.py        ← Updated: Prophet + Gemini + FCM
│   ├── dynamodb_schema.json      ← Updated: added insight field
│   └── AWS_SETUP.md              ← Updated: ThingHTTP webhook setup
├── Cloud/
│   └── THINGSPEAK_SETUP.md       ← Updated: ThingHTTP webhook config
└── docs/superpowers/specs/
    └── 2026-04-25-hardware-v2-design.md  ← this file
```

---

## 9. Implementation Order

```
Step 1 — Hardware wiring + TFT test
  Wire breadboard per pin mapping.
  Flash minimal TFT_eSPI "Hello World" to confirm display works.

Step 2 — ESP8266 firmware
  Full sketch_hardware.ino with ACS712 reading, TFT split layout,
  scrolling graph, LEDs, buzzer, ThingSpeak HTTP POST.

Step 3 — ThingSpeak channel + webhook
  Create channel (4 fields). Configure ThingHTTP to call Lambda URL
  on each new entry.

Step 4 — Lambda update
  Add Prophet + Gemini Flash + anomaly detection + FCM push logic.
  Update DynamoDB schema with insight field.

Step 5 — Flutter app
  Scaffold project. Implement 3 screens, ThingSpeak polling,
  FCM integration, charts.

Step 6 — End-to-end test
  Real current → TFT display → ThingSpeak → Lambda → AI insight →
  FCM push → Flutter app.
```
