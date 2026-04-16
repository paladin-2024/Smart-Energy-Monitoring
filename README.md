# ⚡ Smart Energy Monitoring System

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![License](https://img.shields.io/badge/license-MIT-blue)
![Platform](https://img.shields.io/badge/platform-ESP32-orange)
![Cloud](https://img.shields.io/badge/cloud-AWS%20IoT%20Core-yellow)
![Dashboard](https://img.shields.io/badge/dashboard-Grafana%20Cloud-orange)
![Simulation](https://img.shields.io/badge/simulation-Wokwi-blueviolet)

> A full-stack IoT system that monitors electricity consumption in real time using an ESP32 microcontroller, streams data to ThingSpeak and AWS IoT Core, and applies AI-powered linear regression to predict future power consumption — built in the context of Uganda's energy challenges.

---

## Table of Contents

1. [Project Description](#project-description)
2. [System Architecture](#system-architecture)
3. [Tech Stack](#tech-stack)
4. [Folder Structure](#folder-structure)
5. [Quick Start](#quick-start)
6. [Part A — Wokwi Simulation](#part-a--wokwi-simulation)
7. [Part B — ThingSpeak Cloud](#part-b--thingspeak-cloud)
8. [Part C — AWS IoT + AI](#part-c--aws-iot--ai)
9. [Grafana Dashboard](#grafana-dashboard)
10. [Known Limitations](#known-limitations)
11. [License](#license)

---

## Project Description

Uganda faces chronic load shedding and electricity waste due to lack of real-time consumption visibility. This system addresses that gap by providing an affordable ESP32-based monitor that reads simulated current and voltage sensors, computes power in real time, alerts on overload, streams historical data to the cloud, and uses machine learning to predict future consumption — all visualised on a live Grafana dashboard.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32 (Hardware)                         │
│  Pot1 → ADC34 → 0-240V   Pot2 → ADC35 → 0-10A   P = V × I    │
│  OLED SSD1306 (I2C)   Green LED   Red LED   Buzzer             │
└────────────┬───────────────────────────┬────────────────────────┘
             │ Serial / USB              │ WiFi
             │                           │
    ┌────────▼──────────┐    ┌──────────▼──────────────────────┐
    │  Part A: Wokwi    │    │  Part B: ThingSpeak (HTTP POST) │
    │  Simulation only  │    │  Field1:V  Field2:I  Field3:P   │
    └───────────────────┘    │  Gauge + Line Graph + Alerts    │
                             └──────────────────────────────────┘
                                          │ MQTT / TLS
                             ┌────────────▼──────────────────────┐
                             │  Part C: AWS IoT Core             │
                             │  Topic: energy/monitor            │
                             │        │                          │
                             │   IoT Rule                        │
                             │        │                          │
                             │   AWS Lambda (Python)             │
                             │   ├── Save → DynamoDB             │
                             │   ├── Fetch last 10 readings      │
                             │   ├── Open-Meteo temperature      │
                             │   ├── NumPy linear regression     │
                             │   └── Publish → energy/predictions│
                             │        │                          │
                             │   Lambda Function URL             │
                             │        │                          │
                             │   Grafana Cloud (Infinity plugin) │
                             └───────────────────────────────────┘
```

---

## Tech Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 WROOM-32 |
| Display | OLED SSD1306 128×64 I2C |
| Simulation | Wokwi |
| IoT Libraries | Adafruit SSD1306, Adafruit GFX, PubSubClient, ArduinoJson |
| Cloud Part B | ThingSpeak (HTTP POST) |
| Cloud Part C | AWS IoT Core (MQTT/TLS) |
| Serverless | AWS Lambda (Python 3.11) |
| Database | AWS DynamoDB |
| AI Model | NumPy linear regression |
| External Data | Open-Meteo API (free, no key) |
| Dashboard | Grafana Cloud (Infinity plugin) |
| Diagrams | draw.io |
| Version Control | GitHub |

---

## Folder Structure

```
Smart Energy Monitoring System/
├── README.md
├── Simulation/                  ← Part A (40 marks)
│   ├── sketch.ino               ← ESP32 firmware
│   ├── diagram.json             ← Wokwi circuit
│   ├── CIRCUIT.md               ← Component reference
│   └── SETUP.md                 ← Wokwi setup guide
├── Cloud/                       ← Part B (30 marks)
│   ├── sketch_wifi.ino          ← WiFi + ThingSpeak firmware
│   ├── matlab_analysis.m        ← Moving average MATLAB script
│   └── THINGSPEAK_SETUP.md      ← ThingSpeak setup guide
├── AI&Cloud /                   ← Part C (30 marks)
│   ├── sketch_aws.ino           ← MQTT + AWS IoT firmware
│   ├── lambda_function.py       ← AI Lambda function
│   ├── aws_iot_policy.json      ← IoT device policy
│   ├── POLICY.md                ← Policy explanation
│   ├── dynamodb_schema.json     ← DynamoDB schema
│   ├── SCHEMA.md                ← Schema explanation
│   └── AWS_SETUP.md             ← Complete AWS setup guide
├── diagrams/
│   └── architecture.drawio      ← System architecture diagram
└── Documentation/
    ├── assets/                  ← Images and logos
    ├── smart_energy_monitor.html ← 5-slide presentation
    └── PROJECT_DOCUMENTATION.md ← Full project reference
```

---

## Quick Start

```bash
# 1. Clone the repository
git clone https://github.com/YOUR_USERNAME/smart-energy-monitor.git
cd smart-energy-monitor

# 2. Part A — Run the Wokwi simulation
# See Simulation/SETUP.md for full instructions
# Open https://wokwi.com → New Project → paste diagram.json + sketch.ino

# 3. Part B — Configure ThingSpeak
# See Cloud/THINGSPEAK_SETUP.md
# Update WIFI_SSID, WIFI_PASSWORD, THINGSPEAK_API_KEY in sketch_wifi.ino

# 4. Part C — Deploy to AWS
# See AI&Cloud /AWS_SETUP.md
# Update certificates + endpoint in sketch_aws.ino
# Deploy lambda_function.py to AWS Lambda
```

---

## Part A — Wokwi Simulation

Simulates an ACS712 current sensor (0–10A) and voltage divider (0–240V) using potentiometers connected to ESP32 ADC pins. Computes `P = V × I`, displays real-time readings on a 128×64 OLED, triggers LEDs and buzzer on overload (>150W), and outputs serial data for cloud integration.

**See:** `Simulation/SETUP.md`

---

## Part B — ThingSpeak Cloud

Extends Part A with WiFi connectivity. Posts voltage, current, power, and alert flag to ThingSpeak every 15 seconds via HTTP. Dashboard includes a power trend line graph, live wattage gauge, and voltage numeric display. A React alert sends email notifications when power exceeds 150W. MATLAB analysis overlays a 10-point moving average on the power chart.

**See:** `Cloud/THINGSPEAK_SETUP.md`

---

## Part C — AWS IoT + AI

Publishes MQTT messages to AWS IoT Core over TLS. An IoT Rule triggers a Lambda function that stores readings in DynamoDB, fetches ambient temperature from Open-Meteo, applies a time-of-day threshold multiplier (peak hours: 06–09, 17–21), runs NumPy linear regression on the last 10 readings to predict the next power value, and publishes the prediction back to the `energy/predictions` topic. Grafana Cloud visualises live and predicted data via the Lambda Function URL and Infinity plugin.

**See:** `AI&Cloud /AWS_SETUP.md`

---

## Grafana Dashboard

1. Create a free account at [grafana.com](https://grafana.com)
2. Add **Infinity** data source plugin
3. Point it to your Lambda Function URL
4. Import the two panels: **Power + Prediction time series** and **Readings table**

**See:** `AI&Cloud /AWS_SETUP.md` → Section 7: Grafana Setup

---

## Known Limitations

- **ADC non-linearity:** ESP32 ADC is non-linear at the extremes (0–100mV and 3.1–3.3V). Readings at very low or very high potentiometer positions may be slightly inaccurate. A calibration curve can be applied for production use.
- **ThingSpeak free tier:** Minimum 15-second update interval enforced by ThingSpeak. Data older than 1 year is deleted on free accounts.
- **Linear regression cold start:** The AI prediction requires at least 10 readings in DynamoDB. The first 9 messages will store data without producing a prediction.
- **Open-Meteo rate limit:** Free tier allows 10,000 requests/day. At 15-second intervals, this equates to ~40 hours of continuous operation per day — well within limits.
- **AWS Free Tier:** Lambda (1M requests/month), DynamoDB (25GB storage), IoT Core (250K messages/month) — all within free tier for this project's scale.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

```
Copyright (c) 2026 Caleb
```
