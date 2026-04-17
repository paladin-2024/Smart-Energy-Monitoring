# Smart Energy Monitoring System — Design Spec

**Date:** 2026-04-17
**Author:** Caleb
**Status:** Approved — ready for implementation

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Decisions Made During Brainstorming](#2-decisions-made-during-brainstorming)
3. [Repository Structure](#3-repository-structure)
4. [Asset Inventory](#4-asset-inventory)
5. [Part A — Wokwi Simulation](#5-part-a--wokwi-simulation)
6. [Part B — ThingSpeak Cloud](#6-part-b--thingspeak-cloud)
7. [Part C — AWS IoT + AI Integration](#7-part-c--aws-iot--ai-integration)
8. [Documentation Deliverables](#8-documentation-deliverables)
9. [Commit Strategy](#9-commit-strategy)
10. [Build Approach](#10-build-approach)

---

## 1. Project Overview

A full-stack IoT system that monitors electricity consumption in real time using an ESP32 microcontroller. The system simulates current and voltage sensors using potentiometers, computes power, alerts on overload, publishes data to the cloud, applies AI prediction, and visualises everything on professional dashboards.

**Assignment:** 100 marks total across three parts.
**Target:** 100/100.
**Context:** Uganda load shedding and energy waste — the system provides real-time monitoring and intelligent prediction to help manage consumption.

---

## 2. Decisions Made During Brainstorming

| Decision | Choice | Reason |
|---|---|---|
| Folder structure | Keep existing (`Simulation/`, `Cloud/`, `AI&Cloud /`) | User preference |
| Credentials | All placeholder — starting from scratch | No ThingSpeak or AWS accounts yet |
| Presentation format | HTML first, export to PPTX if needed | Richer visual output |
| Grafana ↔ DynamoDB bridge | Lambda HTTP endpoint + Grafana Infinity plugin | Free tier, clean, no extra AWS services |
| ESP32 firmware split | Two separate sketches — `sketch_wifi.ino` (Part B) and `sketch_aws.ino` (Part C) | Clear separation of concerns, easier marking |
| Icon library | Lucide Icons via CDN | React Icons equivalent for static HTML, no build step |
| OLED display style | Option B — Clean Minimal | All three metrics in one row, oversized power number, bar graph with scale ticks |

---

## 3. Repository Structure

```
Smart Energy Monitoring System/
├── README.md                          ← Professional README with badges
├── Simulation/                        ← Part A (40 marks)
│   ├── sketch.ino
│   ├── diagram.json
│   ├── CIRCUIT.md
│   └── SETUP.md
├── Cloud/                             ← Part B (30 marks)
│   ├── sketch_wifi.ino
│   ├── matlab_analysis.m
│   └── THINGSPEAK_SETUP.md
├── AI&Cloud /                         ← Part C (30 marks)
│   ├── sketch_aws.ino
│   ├── lambda_function.py
│   ├── aws_iot_policy.json
│   ├── POLICY.md
│   ├── dynamodb_schema.json
│   ├── SCHEMA.md
│   └── AWS_SETUP.md
├── diagrams/
│   └── architecture.drawio
└── Documentation/
    ├── assets/                        ← All images (14 files confirmed)
    ├── smart_energy_monitor.html      ← 5-slide visual presentation
    └── PROJECT_DOCUMENTATION.md      ← Comprehensive written reference
```

---

## 4. Asset Inventory

All 14 assets confirmed present in `Documentation/assets/`:

**Component Photos:**
- `esp32.jpg` — ESP32 WROOM-32
- `oled.jpg` — OLED SSD1306 0.96"
- `potentiometer.jpg` — 10kΩ potentiometer ×2
- `led_green.jpg` — 5mm green LED
- `led_red.jpg` — 5mm red LED
- `buzzer.jpg` — Active buzzer 5V
- `breadboard.jpg` — Breadboard
- `ACS712.jpg` — ACS712 current sensor (what the potentiometer simulates)

**Context:**
- `uganda_map.png` — Uganda electricity/load shedding map

**Brand Logos:**
- `aws_logo.jpg`
- `thingspeak_logo.jpg`
- `grafana_logo.jpg`
- `wokwi_logo.png`
- `espressif_logo.jpg`

**Screenshots (to be added after each part runs):**
- `wokwi_screenshot.png` — After Part A
- `circuit_diagram.png` — After Part A (export from Wokwi)
- `thingspeak_dashboard.png` — After Part B
- `aws_iot_console.png` — After Part C
- `grafana_dashboard.png` — After Part C
- `architecture_diagram.png` — Export from diagrams.net after `architecture.drawio` is built

---

## 5. Part A — Wokwi Simulation

### Hardware (diagram.json)

| Component | ESP32 Pin | Role |
|---|---|---|
| Potentiometer 1 | GPIO 34 (ADC1_CH6) | Simulates voltage 0–240V |
| Potentiometer 2 | GPIO 35 (ADC1_CH7) | Simulates current 0–10A |
| OLED SSD1306 | SDA=GPIO21, SCL=GPIO22 | I2C display 128×64 |
| Green LED | GPIO 26 | SAFE indicator (power ≤ 150W) |
| Red LED | GPIO 27 | OVERLOAD indicator (power > 150W) |
| Active Buzzer | GPIO 25 | Audible overload alert |

All LEDs use 220Ω current-limiting resistors.

### ADC Scaling

```
voltage = (analogRead(34) / 4095.0) * 240.0   // 0–240V
current = (analogRead(35) / 4095.0) * 10.0    // 0–10A
power   = voltage * current                     // Watts
alert   = (power > 150) ? 1 : 0
```

### OLED Display Layout — Clean Minimal (Option B)

```
┌────────────────────────────────┐
│    SMART ENERGY MONITOR        │  ← Title, small, letter-spaced
│────────────────────────────────│
│  V 220.5V   I 0.68A   P 150W  │  ← All 3 metrics on one row
│                                │
│         149.9  W               │  ← Power large center (textSize 2)
│          SAFE                  │  ← Status, letter-spaced
│ 0  [████████░░░░░░░] 300W      │  ← Bar graph with scale ticks
└────────────────────────────────┘

OVERLOAD state: "!! OVERLOAD !!" flashes (inverted text — white on black),
bar graph fills completely, buzzer sounds. SSD1306 is monochrome — no colour change.
```

### Serial Output Format

```
voltage:220.50,current:0.68,power:149.94,alert:0
```

### Deliverables

- `Simulation/sketch.ino` — Full firmware (sensors, OLED, LEDs, buzzer, serial)
- `Simulation/diagram.json` — Wokwi circuit definition
- `Simulation/CIRCUIT.md` — Every component, pin, role, why chosen
- `Simulation/SETUP.md` — Step-by-step Wokwi guide + troubleshooting (3 errors)

### Libraries

- `Adafruit SSD1306`
- `Adafruit GFX Library`

---

## 6. Part B — ThingSpeak Cloud

### sketch_wifi.ino — Logic

```
setup()
  ├── WiFi connect (retry loop, status print)
  └── Serial init 115200

loop()
  ├── Read ADC → scale V, I → compute P, alert
  ├── Update OLED + LEDs + buzzer
  ├── WiFi check → reconnect if dropped (graceful)
  ├── Every 15s → HTTP POST to ThingSpeak
  │     field1=V&field2=I&field3=P&field4=alert
  └── Serial debug print
```

### ThingSpeak Channel Fields

| Field | Data | Widget |
|---|---|---|
| Field 1 | Voltage (V) | Numeric display |
| Field 2 | Current (A) | — |
| Field 3 | Power (W) | Line graph + Gauge |
| Field 4 | Alert flag (0/1) | — |

### Dashboard Widgets

1. **Line Graph** — Field 3 (Power) over time, 150W threshold line overlaid
2. **Gauge** — Field 3 (Power), range 0–300W
3. **Numeric** — Field 1 (Voltage)

### Alert

- **ThingSpeak React widget** — triggers email when Field 3 > 150

### MATLAB Analysis

- Moving average window: 10 readings (configurable via `WINDOW_SIZE` constant)
- Overlays smoothed trend on raw power chart
- Auto-triggered on new data point

### Deliverables

- `Cloud/sketch_wifi.ino`
- `Cloud/matlab_analysis.m`
- `Cloud/THINGSPEAK_SETUP.md` — account → channel → widgets → alert → MATLAB

---

## 7. Part C — AWS IoT + AI Integration

### sketch_aws.ino — Logic

```
setup()
  ├── WiFi connect
  ├── Load TLS certificates (device cert + private key + CA root)
  └── MQTT connect to AWS IoT Core endpoint

loop()
  ├── Read sensors → V, I, P, alert
  ├── Build JSON: {"voltage":V,"current":I,"power":P,"alert":A,"timestamp":"..."}
  ├── Publish to MQTT topic: energy/monitor
  ├── MQTT keepalive + reconnect if dropped
  └── Delay 15s
```

### AWS Architecture

```
ESP32 → [MQTT/TLS] → AWS IoT Core
                           │ IoT Rule (SQL: SELECT * FROM 'energy/monitor')
                           ↓
                      AWS Lambda (Python)
                           │
                    ┌──────┴──────┐
                    ↓             ↓
               DynamoDB      IoT Core (publish prediction)
               (store)       topic: energy/predictions
                    │
                    ↓ (HTTP endpoint)
              Grafana Cloud
              (Infinity plugin)
```

### Lambda AI Logic — 8 Steps

1. Save incoming reading to DynamoDB (`deviceId` PK + `timestamp` SK)
2. Query last 10 readings from DynamoDB (sorted by timestamp DESC)
3. Call Open-Meteo API → get current temperature at Kampala, Uganda
4. Apply time-of-day multiplier:
   - Peak hours (06:00–09:00, 17:00–21:00) → threshold × 0.85
   - Off-peak → threshold × 1.0
5. NumPy linear regression on last 10 power readings → predict next value
6. Temperature factor: +0.5% per °C above 25°C
7. Publish prediction JSON back to `energy/predictions` via IoT MQTT
8. HTTP GET endpoint returns last 50 readings as JSON (Grafana Infinity plugin)
   — exposed via Lambda Function URL (free, no API Gateway needed)

### DynamoDB Schema

```
Table: EnergyReadings
  PK: deviceId (String)  — e.g. "esp32-001"
  SK: timestamp (String) — ISO 8601 e.g. "2026-04-17T14:32:00Z"
  Attributes: voltage, current, power, alert, predicted_power, temperature
```

### IoT Policy Permissions

- `iot:Connect` — allow device to connect
- `iot:Publish` — allow publish to `energy/monitor`
- `iot:Subscribe` — allow subscribe to `energy/predictions`
- `iot:Receive` — allow receive messages on `energy/predictions`

### Grafana Dashboard Panels

1. **Time series** — Live power (Field 3) + AI predicted power overlay, 150W threshold line
2. **Table** — Last 20 readings (timestamp, V, I, P, alert flag)

### Deliverables

- `AI&Cloud /sketch_aws.ino`
- `AI&Cloud /lambda_function.py` — Google-style docstrings on every function
- `AI&Cloud /aws_iot_policy.json`
- `AI&Cloud /POLICY.md` — every permission explained
- `AI&Cloud /dynamodb_schema.json`
- `AI&Cloud /SCHEMA.md` — table design + partition key reasoning
- `AI&Cloud /AWS_SETUP.md` — complete step-by-step guide
- `diagrams/architecture.drawio`

---

## 8. Documentation Deliverables

### Documentation/smart_energy_monitor.html

5-slide visual presentation using:
- Lucide Icons via CDN (no React required)
- Real component photos from `assets/`
- Animated SVG charts (power trend, gauge, bar chart, donut)
- Dark professional theme, smooth slide transitions
- Placeholder `<img>` fallbacks for screenshots not yet taken

**Slide content:**
1. Problem — Uganda load shedding, `uganda_map.png`, energy waste stats
2. Part A — Component photos, OLED mockup render, Wokwi screenshot (post-build)
3. Part B — ThingSpeak dashboard mockup, MATLAB overlay chart, brand logo
4. Part C — AWS architecture flow diagram, AI prediction chart, Lambda snippet
5. Impact — Energy savings stats, system summary, conclusion

### Documentation/PROJECT_DOCUMENTATION.md

11 sections covering every part, AI regression math explained in plain English,
DynamoDB schema reasoning, security model, known limitations, future improvements.

---

## 9. Commit Strategy

All commits use Conventional Commits format. User runs each commit manually.

```
1.  feat(part-a): add ESP32 OLED simulation with sensor scaling and overload detection
2.  docs(part-a): add CIRCUIT.md and SETUP.md with Wokwi walkthrough
3.  feat(part-b): add WiFi sketch with ThingSpeak HTTP POST and graceful reconnect
4.  feat(part-b): add MATLAB moving average analysis script
5.  docs(part-b): add THINGSPEAK_SETUP.md with full dashboard and alert guide
6.  feat(part-c): add AWS IoT MQTT sketch with TLS certificate support
7.  feat(part-c): add Lambda AI function with linear regression and Open-Meteo
8.  feat(part-c): add DynamoDB schema, IoT policy, and architecture diagram
9.  docs(part-c): add AWS_SETUP.md, POLICY.md, and SCHEMA.md
10. docs: add professional README with badges and full tech stack table
11. feat(presentation): add 5-slide HTML presentation and PROJECT_DOCUMENTATION.md
```

---

## 10. Build Approach

**Approach B — Part-by-part, fully complete.**

Each part is 100% done (code + docs) before moving to the next. Order:

```
Part A complete → commit 1 + 2
Part B complete → commit 3 + 4 + 5
Part C complete → commit 6 + 7 + 8 + 9
README          → commit 10
Presentation    → commit 11
```

After each part, user adds screenshots to `Documentation/assets/` as the system
becomes live.
