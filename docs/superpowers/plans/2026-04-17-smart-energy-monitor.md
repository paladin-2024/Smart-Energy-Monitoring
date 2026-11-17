# Smart Energy Monitoring System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete 3-part IoT energy monitoring system (Wokwi simulation → ThingSpeak cloud → AWS IoT + AI) targeting 100/100 marks.

**Architecture:** ESP32 reads simulated voltage/current via potentiometers, displays on SSD1306 OLED, sends data to ThingSpeak via HTTP and AWS IoT Core via MQTT. A Python Lambda applies NumPy linear regression + Open-Meteo temperature data to predict power consumption and exposes results to Grafana via Lambda Function URL.

**Tech Stack:** Arduino C++ (ESP32), Python 3.11 (Lambda), AWS IoT Core, DynamoDB, Grafana Cloud (Infinity plugin), ThingSpeak, MATLAB, Wokwi, draw.io, Lucide Icons CDN, HTML/CSS/SVG

---

## File Map

```
Simulation/sketch.ino               — Part A firmware (sensors, OLED, LEDs, buzzer, serial)
Simulation/diagram.json             — Wokwi circuit definition
Simulation/CIRCUIT.md               — Component reference
Simulation/SETUP.md                 — Wokwi setup guide + troubleshooting

Cloud/sketch_wifi.ino               — Part B firmware (WiFi + ThingSpeak HTTP POST)
Cloud/matlab_analysis.m             — Moving average MATLAB script
Cloud/THINGSPEAK_SETUP.md           — ThingSpeak full setup guide

AI&Cloud /sketch_aws.ino            — Part C firmware (MQTT + TLS + AWS IoT)
AI&Cloud /lambda_function.py        — Python AI + DynamoDB + Open-Meteo + Grafana endpoint
AI&Cloud /aws_iot_policy.json       — IoT device policy
AI&Cloud /POLICY.md                 — Policy explanation
AI&Cloud /dynamodb_schema.json      — DynamoDB table definition
AI&Cloud /SCHEMA.md                 — Schema explanation
AI&Cloud /AWS_SETUP.md              — Complete AWS setup guide

diagrams/architecture.drawio        — Full system architecture diagram

README.md                           — Professional readme with badges
Documentation/smart_energy_monitor.html  — 5-slide visual presentation
Documentation/PROJECT_DOCUMENTATION.md  — 11-section comprehensive reference
```

---

## Task 1: Project Scaffold

**Files:** `.gitignore`, root `README.md` stub

- [ ] Create `.gitignore`
- [ ] Update `README.md` stub
- [ ] Commit: `chore: initialise project structure and gitignore`

---

## Task 2: Part A — sketch.ino

**Files:** `Simulation/sketch.ino`

- [ ] Write full ESP32 firmware with sensor scaling, OLED display, LEDs, buzzer, serial output
- [ ] Verify in Wokwi — check SAFE and OVERLOAD states
- [ ] Verify serial output format: `voltage:xxx.xx,current:x.xx,power:xxx.xx,alert:0or1`

---

## Task 3: Part A — diagram.json

**Files:** `Simulation/diagram.json`

- [ ] Write Wokwi circuit JSON (ESP32 + OLED + 2×pot + 2×LED + buzzer + 2×resistor)
- [ ] Load in Wokwi and confirm all components connect and simulate

---

## Task 4: Part A — Docs

**Files:** `Simulation/CIRCUIT.md`, `Simulation/SETUP.md`

- [ ] Write CIRCUIT.md — every component, pin, role, why chosen
- [ ] Write SETUP.md — step-by-step Wokwi guide + 3 troubleshooting errors
- [ ] Commit: `feat(part-a): add ESP32 OLED simulation with sensor scaling and overload detection`
- [ ] Commit: `docs(part-a): add CIRCUIT.md and SETUP.md with Wokwi walkthrough`

---

## Task 5: Part B — sketch_wifi.ino

**Files:** `Cloud/sketch_wifi.ino`

- [ ] Write WiFi sketch extending Part A with graceful reconnect + ThingSpeak HTTP POST every 15s
- [ ] Verify serial shows WiFi connect + POST success messages

---

## Task 6: Part B — MATLAB + Docs

**Files:** `Cloud/matlab_analysis.m`, `Cloud/THINGSPEAK_SETUP.md`

- [ ] Write MATLAB moving average script (window=10, configurable WINDOW_SIZE constant)
- [ ] Write THINGSPEAK_SETUP.md (account → channel → widgets → React alert → MATLAB)
- [ ] Commit: `feat(part-b): add WiFi sketch with ThingSpeak HTTP POST and graceful reconnect`
- [ ] Commit: `feat(part-b): add MATLAB moving average analysis script`
- [ ] Commit: `docs(part-b): add THINGSPEAK_SETUP.md with full dashboard and alert guide`

---

## Task 7: Part C — sketch_aws.ino

**Files:** `AI&Cloud /sketch_aws.ino`

- [ ] Write MQTT sketch with TLS certificate placeholders, JSON payload, keepalive reconnect
- [ ] Verify serial shows MQTT connect + publish confirmation

---

## Task 8: Part C — lambda_function.py

**Files:** `AI&Cloud /lambda_function.py`

- [ ] Write full Lambda: save to DynamoDB → query last 10 → Open-Meteo → regression → publish prediction → HTTP endpoint for Grafana
- [ ] Write pytest unit tests covering regression logic and time-of-day multiplier

---

## Task 9: Part C — Policy, Schema, Architecture

**Files:** `AI&Cloud /aws_iot_policy.json`, `AI&Cloud /dynamodb_schema.json`, `diagrams/architecture.drawio`

- [ ] Write IoT policy JSON (Connect + Publish + Subscribe + Receive)
- [ ] Write DynamoDB schema JSON (deviceId PK + timestamp SK)
- [ ] Write architecture.drawio XML (ESP32 → IoT Core → Lambda → DynamoDB → Grafana)

---

## Task 10: Part C — Docs

**Files:** `AI&Cloud /POLICY.md`, `AI&Cloud /SCHEMA.md`, `AI&Cloud /AWS_SETUP.md`

- [ ] Write POLICY.md — every permission explained
- [ ] Write SCHEMA.md — table design + partition key reasoning
- [ ] Write AWS_SETUP.md — complete step-by-step guide
- [ ] Commit: `feat(part-c): add AWS IoT MQTT sketch with TLS certificate support`
- [ ] Commit: `feat(part-c): add Lambda AI function with linear regression and Open-Meteo`
- [ ] Commit: `feat(part-c): add DynamoDB schema, IoT policy, and architecture diagram`
- [ ] Commit: `docs(part-c): add AWS_SETUP.md, POLICY.md, and SCHEMA.md`

---

## Task 11: README + Presentation + Docs

**Files:** `README.md`, `Documentation/smart_energy_monitor.html`, `Documentation/PROJECT_DOCUMENTATION.md`

- [ ] Write professional README with badges, architecture, tech stack table, quick start
- [ ] Write 5-slide HTML presentation (real component photos + animated SVG charts + Lucide icons)
- [ ] Write PROJECT_DOCUMENTATION.md (11 sections, full project reference)
- [ ] Commit: `docs: add professional README with badges and full tech stack table`
- [ ] Commit: `feat(presentation): add 5-slide HTML presentation and PROJECT_DOCUMENTATION.md`
