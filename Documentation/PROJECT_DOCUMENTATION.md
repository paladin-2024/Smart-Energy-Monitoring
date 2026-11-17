# Smart Energy Monitoring System — Project Documentation

**Author:** Caleb  
**Date:** 2026-04-17  
**Version:** 1.0.0  
**Course:** IoT Systems Design  
**Total Marks:** 100

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Part A — Hardware Simulation](#3-part-a--hardware-simulation)
4. [Part B — ThingSpeak Cloud Dashboard](#4-part-b--thingspeak-cloud-dashboard)
5. [Part C — AWS IoT Core + AI](#5-part-c--aws-iot-core--ai)
6. [AI Regression Model — Explained](#6-ai-regression-model--explained)
7. [DynamoDB Schema — Design Decisions](#7-dynamodb-schema--design-decisions)
8. [Security Model](#8-security-model)
9. [File Structure](#9-file-structure)
10. [Known Limitations](#10-known-limitations)
11. [Future Improvements](#11-future-improvements)

---

## 1. Project Overview

The Smart Energy Monitoring System is a three-part IoT pipeline that simulates a real-world home energy monitor. It measures voltage and current using an ESP32 microcontroller, computes power consumption, detects overload conditions, uploads data to cloud platforms, and uses a machine-learning model to predict future power usage.

### Goals

| Goal | How Achieved |
|---|---|
| Real-time energy monitoring | ESP32 reads ADC every 500ms, displays on OLED |
| Cloud storage and visualisation | ThingSpeak (Part B) + DynamoDB via Lambda (Part C) |
| Automated alerts | Email via ThingSpeak React widget; MQTT publish back to device |
| AI-based prediction | NumPy linear regression in AWS Lambda |
| Context-aware adjustments | Open-Meteo weather API + time-of-day multiplier |
| Zero cost | All services within AWS Free Tier + ThingSpeak free plan |

### Marks Allocation

| Part | Topic | Marks |
|---|---|---|
| A | Hardware simulation (Wokwi, OLED, ADC, alerts) | 40 |
| B | ThingSpeak cloud + MATLAB analysis | 30 |
| C | AWS IoT Core, Lambda AI, DynamoDB, Grafana | 30 |
| **Total** | | **100** |

---

## 2. System Architecture

```
┌─────────────┐    ADC GPIO34/35     ┌───────────────────────────────────┐
│             │  ──────────────────► │  Wokwi Simulation                 │
│  ESP32      │                      │  Potentiometers → Voltage/Current  │
│  WROOM-32   │◄─── OLED I2C ──────  │  SSD1306 128×64 display           │
│             │◄─── LED GPIO25/26 ─  │  Green=SAFE, Red=OVERLOAD          │
│             │◄─── Buzzer GPIO27 ─  │  Buzzer on overload               │
└──────┬──────┘                      └───────────────────────────────────┘
       │
       │ Part B: HTTP POST (WiFi)
       ▼
┌──────────────┐   MATLAB Analysis   ┌─────────────────────────────────┐
│  ThingSpeak  │ ──────────────────► │  movmean(10) smoothing           │
│  4 channels  │                     │  Overload markers               │
│  (V,I,P,Alrt)│                     │  Summary statistics             │
│              │ ──── React Alert ─► │  Email on field4 = 1            │
└──────────────┘                     └─────────────────────────────────┘
       │
       │ Part C: MQTT/TLS port 8883
       ▼
┌───────────────┐   IoT Rule      ┌───────────────────────────────────┐
│  AWS IoT Core │ ─────────────►  │  Lambda (Python 3.11)              │
│  energy/      │                 │  1. save_reading → DynamoDB        │
│  monitor      │                 │  2. get_weather → Open-Meteo       │
│               │◄── predictions  │  3. linear regression (NumPy)      │
│               │    (MQTT back)  │  4. publish prediction             │
└───────────────┘                 └────────────┬──────────────────────┘
                                               │ PutItem
                                               ▼
                                  ┌────────────────────────┐
                                  │  DynamoDB              │
                                  │  EnergyReadings        │
                                  │  PK: deviceId          │
                                  │  SK: timestamp (ISO)   │
                                  └────────────┬───────────┘
                                               │ HTTP GET (Function URL)
                                               ▼
                                  ┌────────────────────────┐
                                  │  Grafana Cloud         │
                                  │  Infinity plugin       │
                                  │  Time series + Table   │
                                  └────────────────────────┘
```

### Data Flow Summary

1. **ESP32** reads two ADC pins every 500ms and computes power.
2. **Part B:** Every 15s the device posts to ThingSpeak via WiFi HTTP GET. MATLAB analyses the channel.
3. **Part C:** Every 15s the device publishes a JSON MQTT message to AWS IoT Core over TLS port 8883.
4. An **IoT Rule** triggers Lambda synchronously.
5. **Lambda** saves the reading to DynamoDB, fetches the current temperature from Open-Meteo, runs linear regression on the last 10 readings, applies corrections, then publishes a prediction back to the device.
6. **Grafana** calls the Lambda Function URL (HTTP GET) and renders live dashboards via the Infinity plugin.

---

## 3. Part A — Hardware Simulation

### 3.1 Why Wokwi?

Wokwi is a browser-based ESP32/Arduino simulator that supports real component models including the SSD1306 OLED, potentiometers, LEDs, and buzzers. It allows the project to be demonstrated without physical hardware while behaving identically to a real circuit.

### 3.2 ADC Pin Choice

The ESP32 has two ADC modules: ADC1 (GPIO32–39) and ADC2 (GPIO0, 2, 4, 12–15, 25–27). **ADC2 is disabled when the WiFi radio is active** because the two share internal circuitry. This is a known hardware limitation documented by Espressif.

For this reason, **GPIO34 and GPIO35 are used** — both are ADC1 pins and work correctly even when WiFi is connected in Parts B and C. GPIO34 and GPIO35 are also input-only pins (no internal pull-up), which makes them electrically clean for ADC use.

### 3.3 ADC Scaling

The ESP32 ADC returns a 12-bit integer (0–4095) representing a voltage from 0V to 3.3V. The firmware maps this to physical units:

```
voltage = (rawVoltage / 4095.0) × 240    → 0 to 240 V
current = (rawCurrent / 4095.0) × 10     → 0 to 10 A
power   = voltage × current               → 0 to 2400 W
alert   = (power > 150.0) ? 1 : 0
```

A 150W overload threshold is used because Uganda's household circuits often run on 5A fuses at 220V (1100W), and a student apartment load of 150W represents a realistic monitoring point.

### 3.4 OLED Display

The SSD1306 is a **monochrome** display — it cannot show colour. The display is connected via I2C at address 0x3C on GPIO21 (SDA) and GPIO22 (SCL). The Adafruit SSD1306 and Adafruit GFX libraries are used.

The "OVERLOAD" state is shown using inverted text (white-on-black block) because that is the only available visual emphasis on a monochrome display. The word flashes by toggling the display mode.

**OLED layout:**
```
SMART ENERGY MON.
─────────────────
V: 220.50 V
I:   0.68 A
P: 149.94 W
Status: SAFE
2026-04-17 14:32
```

### 3.5 Alert Logic

When `power > 150W`:
- Red LED (GPIO26) turns ON
- Green LED (GPIO25) turns OFF
- Buzzer (GPIO27) activates with a 1 kHz tone
- OLED shows "OVERLOAD" in inverted text block
- `alert = 1` is included in the MQTT/HTTP payload

### 3.6 Serial Output Format

Every reading is printed in a parseable format for debugging:
```
voltage:220.50,current:0.68,power:149.94,alert:0
```

### 3.7 Files

| File | Purpose |
|---|---|
| `Simulation/sketch.ino` | Complete Part A firmware |
| `Simulation/diagram.json` | Wokwi circuit definition |
| `Simulation/CIRCUIT.md` | Component list, pin table, design decisions |
| `Simulation/SETUP.md` | Step-by-step Wokwi setup guide |

---

## 4. Part B — ThingSpeak Cloud Dashboard

### 4.1 Why ThingSpeak?

ThingSpeak is a free IoT analytics platform by MathWorks. Its key advantage over generic cloud storage is built-in MATLAB analysis — every channel includes a free MATLAB code editor that runs in MathWorks cloud. This allows moving average computation, statistical summaries, and overload detection without setting up a separate data processing service.

### 4.2 Channel Configuration

| Field | Variable | Unit | Notes |
|---|---|---|---|
| field1 | Voltage | V | 2 decimal places |
| field2 | Current | A | 2 decimal places |
| field3 | Power | W | 2 decimal places |
| field4 | Alert | — | 0 = SAFE, 1 = OVERLOAD |

### 4.3 Upload Mechanism

The firmware uses the ESP32's `HTTPClient` library to make an HTTP GET request to the ThingSpeak update API:

```
GET https://api.thingspeak.com/update?api_key=KEY&field1=V&field2=I&field3=P&field4=alert
```

**Why HTTP GET instead of POST?** ThingSpeak's update API accepts both. GET is simpler for the ESP32's HTTPClient — no body or Content-Type header required.

**15-second interval:** ThingSpeak enforces a minimum 15-second update interval on free accounts. The firmware uses `millis()` for non-blocking timing — the sensor loop continues reading at 500ms while uploads happen only when `millis() - lastUploadTime >= 15000`.

### 4.4 WiFi Reconnect Logic

If WiFi drops (power fluctuation, router restart), the firmware automatically reconnects:

```cpp
while (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    delay(100);
    connectWiFi();   // reconnect with 10s timeout
}
```

The disconnect/reconnect cycle is important — simply retrying `WiFi.begin()` without disconnecting first can cause the ESP32 WiFi stack to get stuck.

### 4.5 MATLAB Analysis

The MATLAB Analysis script (`Cloud/matlab_analysis.m`) is pasted into the ThingSpeak channel's MATLAB Analysis tab:

1. Reads the last 200 data points from field3 (power)
2. Applies `movmean(data, 10, 'Endpoints', 'shrink')` to smooth noise
3. Finds all overload events with `find(data > 150)`
4. Computes summary statistics (mean, max, std)
5. Plots raw data, smoothed line, and overload markers

**Why `'Endpoints', 'shrink'`?** At the start of the dataset (fewer than 10 samples), `movmean` with `'shrink'` uses whatever samples are available rather than padding with zeros, which would distort the early values.

### 4.6 Alert Configuration

ThingSpeak's **React** feature sends an email when field4 equals 1. It checks continuously and triggers once per overload event (not repeatedly while the alert is active).

### 4.7 Files

| File | Purpose |
|---|---|
| `Cloud/sketch_wifi.ino` | Part B firmware (WiFi + ThingSpeak) |
| `Cloud/matlab_analysis.m` | MATLAB moving average analysis script |
| `Cloud/THINGSPEAK_SETUP.md` | Channel setup, MATLAB, React, troubleshooting |

---

## 5. Part C — AWS IoT Core + AI

### 5.1 Why AWS IoT Core?

AWS IoT Core provides a managed MQTT broker with mutual TLS authentication, fine-grained access control via IoT policies, and direct integration with Lambda via IoT Rules. It replaces a self-hosted MQTT broker while providing enterprise-grade security and scaling.

### 5.2 MQTT/TLS Connection

The ESP32 connects to AWS IoT Core using:
- **Protocol:** MQTT over TLS 1.2
- **Port:** 8883
- **Authentication:** X.509 mutual authentication (client certificate + private key)
- **Library:** PubSubClient (Arduino)

Three certificate files are embedded in the firmware as PROGMEM char arrays to avoid loading them from flash at runtime:
- `AmazonRootCA1.pem` → validates the server's identity
- `xxxxxxxxxx-certificate.pem.crt` → device's identity certificate
- `xxxxxxxxxx-private.pem.key` → device's private key (never leaves the device)

**NTP sync is mandatory.** TLS certificate validation checks that the current time falls within the certificate's validity period. If the ESP32 clock is wrong (it has no RTC), the TLS handshake fails. The firmware syncs to `pool.ntp.org` before connecting to MQTT.

### 5.3 MQTT Topics

| Topic | Direction | Purpose |
|---|---|---|
| `energy/monitor` | ESP32 → IoT Core | Sensor readings (JSON, every 15s) |
| `energy/predictions` | IoT Core → ESP32 | Lambda prediction result (JSON) |

### 5.4 JSON Payload (ESP32 → Lambda)

```json
{
  "deviceId":  "esp32-001",
  "voltage":   220.50,
  "current":   0.68,
  "power":     149.94,
  "alert":     0,
  "timestamp": "2026-04-17T14:32:00Z"
}
```

The timestamp is formatted by the ESP32 after NTP sync using `strftime()`.

### 5.5 IoT Rule

The IoT Rule `EnergyMonitorRule` uses the SQL:

```sql
SELECT * FROM 'energy/monitor'
```

This passes every field from the MQTT JSON payload to Lambda as the `event` object. No transformation is required — Lambda receives the dictionary directly.

### 5.6 Lambda Function Architecture

The Lambda function (`AI&Cloud /lambda_function.py`) handles two event types:

**IoT event** (triggered by IoT Rule every 15s):
1. Parse the MQTT payload
2. Write the reading to DynamoDB
3. Fetch temperature from Open-Meteo
4. Query the last 10 readings from DynamoDB
5. Run linear regression
6. Apply temperature and time-of-day corrections
7. Update DynamoDB item with `predicted_power`
8. Publish prediction to `energy/predictions`

**HTTP GET event** (called by Grafana):
1. Query the last 50 readings from DynamoDB
2. Return as a JSON array

**Warm-start pattern:** `boto3` clients are initialised outside the handler function at module level. Lambda reuses the same execution environment for subsequent invocations, so the clients are created only once — reducing cold-start latency.

### 5.7 Lambda Function URL

Instead of API Gateway, the Lambda uses a **Function URL** — a free HTTPS endpoint built into Lambda. This exposes the function at:
```
https://xxxxxxxxxxxxxxxx.lambda-url.us-east-1.on.aws/
```
Auth type is `NONE` so Grafana's Infinity plugin can call it without authentication headers. CORS is enabled with `Allow-Origin: *`.

### 5.8 Files

| File | Purpose |
|---|---|
| `AI&Cloud /sketch_aws.ino` | Part C firmware (MQTT/TLS + predictions) |
| `AI&Cloud /lambda_function.py` | Lambda AI function |
| `AI&Cloud /aws_iot_policy.json` | Least-privilege IoT policy JSON |
| `AI&Cloud /dynamodb_schema.json` | DynamoDB CreateTable definition |
| `AI&Cloud /AWS_SETUP.md` | Complete AWS setup guide (10 sections) |
| `AI&Cloud /POLICY.md` | IoT policy permission-by-permission explanation |
| `AI&Cloud /SCHEMA.md` | DynamoDB key design rationale |

---

## 6. AI Regression Model — Explained

### 6.1 What Is Linear Regression?

Linear regression finds the straight line that best fits a set of data points. Given power readings at time positions 0, 1, 2, ..., N-1, it finds:

```
power = slope × time + intercept
```

To predict the next reading (at position N):
```
predicted_power = slope × N + intercept
```

This works well for short-term power trends because appliance load changes gradually over 15-second intervals — a washing machine spinning up, a kettle heating water, an air conditioner cycling.

### 6.2 NumPy Implementation

```python
# readings = last 10 DynamoDB items, ordered oldest-first
power_values = [float(r["power"]) for r in reversed(readings)]

x = np.arange(len(power_values), dtype=float)   # [0, 1, 2, ..., 9]
y = np.array(power_values, dtype=float)

coefficients = np.polyfit(x, y, 1)   # degree-1 polynomial
slope        = coefficients[0]
intercept    = coefficients[1]

next_x    = float(len(power_values))             # = 10
predicted = slope * next_x + intercept
predicted = max(0.0, predicted)                   # clamp negative values
```

`np.polyfit(x, y, 1)` uses the least-squares method — it minimises the sum of squared differences between the fitted line and the actual readings. This is the standard linear regression formula.

### 6.3 Temperature Correction

Electrical load increases with ambient temperature because cooling systems (fans, AC) work harder in hot weather. The correction adds 0.5% per degree above 25°C:

```python
def apply_temperature_correction(predicted, temperature):
    if temperature > 25.0:
        correction = 1.0 + (temperature - 25.0) * 0.005
        return predicted * correction
    return predicted
```

**Example:** If the predicted power is 150W and the temperature is 28°C:
```
correction = 1.0 + (28 - 25) × 0.005 = 1.015
adjusted   = 150 × 1.015 = 152.25W
```

Temperature comes from the [Open-Meteo](https://open-meteo.com) free weather API using Kampala coordinates (0.3163°N, 32.5822°E). If the API is unavailable, the function falls back to 25°C (no correction applied).

### 6.4 Time-of-Day Multiplier

During peak hours (morning 6–9 EAT, evening 17–21 EAT), households use more power. The alert threshold is tightened by 15% to catch near-overload conditions earlier:

```python
PEAK_HOURS = [(6, 9), (17, 21)]   # EAT = UTC+2

def get_time_of_day_multiplier():
    eat_hour = (datetime.utcnow().hour + 2) % 24
    for start, end in PEAK_HOURS:
        if start <= eat_hour < end:
            return 0.85   # tighter threshold during peak hours
    return 1.0
```

The multiplier applies to the **threshold comparison**, not the prediction itself. A 0.85 multiplier means the effective overload threshold during peak hours becomes 150 × 0.85 = 127.5W.

### 6.5 Why Linear Regression and Not a Neural Network?

| Factor | Linear Regression | Neural Network |
|---|---|---|
| Minimum data required | 2 readings | Hundreds to thousands |
| Compute time | < 1ms | Seconds to train |
| Lambda cold start | Minimal | Large model load |
| Interpretability | Slope tells you trend direction | Black box |
| Accuracy for 15s window | Good — trends are nearly linear | Overkill |

For 15-second power intervals, the relationship between time and power is approximately linear. A neural network would require far more data and computation for no meaningful accuracy gain.

---

## 7. DynamoDB Schema — Design Decisions

### 7.1 Table Structure

| Property | Value | Reason |
|---|---|---|
| Table name | `EnergyReadings` | Describes content clearly |
| Partition key | `deviceId` (String) | Groups all readings per device |
| Sort key | `timestamp` (String, ISO 8601) | Enables time-range queries |
| Billing mode | PAY_PER_REQUEST | Free tier friendly, no capacity planning |

### 7.2 Why a Composite Key?

DynamoDB is a key-value store with optional range queries. The composite key (partition key + sort key) enables:

1. **Efficient device queries** — all readings for `esp32-001` are in the same partition; no table scan needed.
2. **Chronological ordering** — items are stored sorted by timestamp within a partition.
3. **Latest-N queries** — `ScanIndexForward=False, Limit=10` returns the 10 most recent readings in one API call.

### 7.3 Why ISO 8601 String Timestamps?

ISO 8601 strings like `2026-04-17T14:32:00Z` sort lexicographically in the same order as chronologically:
```
"2026-04-17T14:32:00Z" < "2026-04-17T14:32:15Z" < "2026-04-17T14:33:00Z"
```
This means DynamoDB's natural string sort order is chronological order. Unix epoch integers would also work but are not human-readable in the DynamoDB console or Grafana table panels.

### 7.4 Item Schema

```json
{
  "deviceId":        "esp32-001",
  "timestamp":       "2026-04-17T14:32:00Z",
  "voltage":         220.50,
  "current":         0.68,
  "power":           149.94,
  "alert":           0,
  "predicted_power": 153.20,
  "temperature":     26.5
}
```

`predicted_power` and `temperature` are added by Lambda in the same invocation that writes the initial reading. DynamoDB is schemaless — only the key attributes are required; all others are optional. In practice, every item has all attributes.

### 7.5 Why Not a Global Secondary Index (GSI)?

All Lambda queries follow the same access pattern: get recent readings for a specific `deviceId`. The base table composite key covers this exactly. A GSI adds cost and complexity for no benefit in this access pattern.

---

## 8. Security Model

### 8.1 Transport Security

All MQTT communication between the ESP32 and AWS IoT Core uses **TLS 1.2 with mutual authentication**:

- The ESP32 presents its device certificate during the TLS handshake — this proves its identity to AWS.
- AWS presents its server certificate (signed by Amazon Root CA 1) — the ESP32 verifies this using the embedded root CA.
- Mutual authentication means neither side can be impersonated.
- Port 8883 is the IANA-assigned port for MQTT over TLS.

### 8.2 IoT Policy — Principle of Least Privilege

The IoT policy grants exactly four permissions, nothing more:

| Permission | Resource | Purpose |
|---|---|---|
| `iot:Connect` | `client/esp32-001` | Connect using this specific client ID |
| `iot:Publish` | `topic/energy/monitor` | Send sensor readings |
| `iot:Subscribe` | `topicfilter/energy/predictions` | Register interest in predictions |
| `iot:Receive` | `topic/energy/predictions` | Receive delivered messages |

A compromised device cannot publish to `energy/predictions` (which would allow it to spoof AI predictions), subscribe to other devices' topics, or perform any management operations.

### 8.3 Certificate Management

- The device private key (`xxxxxxxxxx-private.pem.key`) is embedded in firmware. It never leaves the device and cannot be retrieved from AWS after download.
- The certificate is tied to a specific IoT Thing (`esp32-001`) and can be revoked immediately from the AWS console if the device is compromised.
- Certificates are stored as PROGMEM arrays in firmware (not in SPIFFS) to avoid flash read complexity.

### 8.4 Lambda IAM Role

The Lambda execution role has only two managed policies:
- `AmazonDynamoDBFullAccess` — needed to read/write the EnergyReadings table
- `AWSIoTDataAccess` — needed to publish to `energy/predictions`

No S3, EC2, or other service access is granted.

### 8.5 Grafana Function URL

The Lambda Function URL has auth type `NONE`. This is acceptable because:
- The URL is a random 16-character subdomain (not guessable)
- The function returns only aggregated sensor data — no PII, no credentials
- The only operation available via GET is reading recent readings
- Write operations are only possible via MQTT (requires certificate)

---

## 9. File Structure

```
Smart Energy Monitoring System/
│
├── README.md                          # Project overview with badges
│
├── Simulation/                        # Part A — Hardware simulation
│   ├── sketch.ino                     # ESP32 firmware (ADC, OLED, alerts)
│   ├── diagram.json                   # Wokwi circuit definition
│   ├── CIRCUIT.md                     # Component list and pin table
│   └── SETUP.md                       # Wokwi setup guide
│
├── Cloud/                             # Part B — ThingSpeak
│   ├── sketch_wifi.ino                # ESP32 firmware (WiFi + ThingSpeak)
│   ├── matlab_analysis.m              # MATLAB moving average script
│   └── THINGSPEAK_SETUP.md            # ThingSpeak setup guide
│
├── AI&Cloud /                         # Part C — AWS IoT + AI
│   ├── sketch_aws.ino                 # ESP32 firmware (MQTT/TLS)
│   ├── lambda_function.py             # Lambda AI function
│   ├── aws_iot_policy.json            # IoT Core policy document
│   ├── dynamodb_schema.json           # DynamoDB table definition
│   ├── AWS_SETUP.md                   # AWS setup guide (10 sections)
│   ├── POLICY.md                      # IoT policy explanation
│   └── SCHEMA.md                      # DynamoDB schema explanation
│
├── diagrams/
│   └── architecture.drawio            # Full system architecture diagram
│
├── Documentation/
│   ├── smart_energy_monitor.html      # 5-slide HTML presentation
│   ├── PROJECT_DOCUMENTATION.md       # This file
│   └── assets/                        # Component photos + logos
│       ├── esp32.jpg
│       ├── oled.jpg
│       ├── ACS712.jpg
│       ├── potentiometer.jpg
│       ├── led_green.jpg
│       ├── led_red.jpg
│       ├── buzzer.jpg
│       ├── breadboard.jpg
│       ├── uganda_map.png
│       ├── espressif_logo.jpg
│       ├── wokwi_logo.png
│       ├── thingspeak_logo.jpg
│       ├── aws_logo.jpg
│       └── grafana_logo.jpg
│
└── docs/superpowers/
    ├── specs/2026-04-17-smart-energy-monitor-design.md
    └── plans/2026-04-17-smart-energy-monitor.md
```

---

## 10. Known Limitations

### 10.1 ADC Non-Linearity

The ESP32 ADC is non-linear, especially at the low end (0–100mV) and near the top (3.1–3.3V). This causes voltage/current readings to be slightly inaccurate near the extremes of the potentiometer range. In a production system, an op-amp buffer or external ADC (e.g. ADS1115) would be used.

### 10.2 Wokwi vs Real Hardware

Wokwi simulates ideal components. A real ACS712 current sensor introduces DC offset, noise, and temperature drift that the simulation does not reproduce. The potentiometer simulation also produces perfect linear ADC values, while a real circuit would show switching noise.

### 10.3 Single-Device Design

The DynamoDB schema supports multiple devices (each `deviceId` gets its own partition), but the Lambda function only queries `DEVICE_ID = "esp32-001"`. Extending to multiple devices would require querying by each device ID individually or using a DynamoDB Global Secondary Index on a different access pattern.

### 10.4 Regression Requires 2+ Readings

`np.polyfit` requires at least 2 data points to fit a line. On Lambda's first invocation, only 1 reading exists in DynamoDB. The function handles this gracefully by returning the current power value as the prediction if fewer than 2 readings are available.

### 10.5 Open-Meteo Rate Limits

Open-Meteo allows up to 10,000 free API calls per day. At 15-second intervals, the system makes 5,760 calls per day — safely under the limit. However, if the system is scaled to multiple devices calling Lambda simultaneously, the rate limit could be approached.

### 10.6 ThingSpeak 15-Second Minimum

ThingSpeak's free tier enforces a 15-second minimum upload interval. Faster polling (e.g. 5s) is not possible without a paid account. For rapid overload detection, the OLED display and buzzer (which update every 500ms) are more responsive than the cloud dashboard.

### 10.7 Lambda Cold Start

Lambda functions that haven't been invoked recently take 0.5–2s to start ("cold start"). The first MQTT message after a quiet period may experience higher latency. Subsequent invocations within the same execution environment are fast (warm start). This does not affect correctness, only the latency of the first prediction.

---

## 11. Future Improvements

### 11.1 Multiple Devices

Deploy the same firmware to multiple ESP32 devices with unique `DEVICE_ID` values. Lambda already writes `deviceId` as the partition key, so readings from all devices are isolated and queryable independently. The Grafana dashboard would need additional panels per device.

### 11.2 LSTM Model

Replace the linear regression with an LSTM (Long Short-Term Memory) neural network for more accurate predictions over longer time horizons. This would require:
- Storing 100+ readings per device
- A separate model training pipeline (e.g. SageMaker)
- Deploying the trained model to Lambda as a pickle file

Linear regression is the right choice for this project at 15-second intervals, but LSTM would outperform it for hourly or daily trend prediction.

### 11.3 SPIFFS Certificate Storage

Instead of embedding certificates as PROGMEM char arrays, store them in the ESP32's SPIFFS filesystem. This allows certificate rotation without reflashing the firmware — the device downloads a new certificate and writes it to SPIFFS via an OTA update.

### 11.4 Real Current Sensor

Replace the potentiometer simulation with a real ACS712-5A current sensor wired to GPIO34. The ACS712 outputs 2.5V at 0A and scales at 185mV/A, so the ADC scaling formula changes to:

```cpp
float voltage_v = (raw / 4095.0) * 3.3;      // ADC voltage
float current   = (voltage_v - 2.5) / 0.185; // ACS712 scaling
```

### 11.5 OTA Firmware Updates

Add AWS IoT Jobs + Arduino OTA to enable remote firmware updates. This is essential for production deployments where physical access to the device is impractical.

### 11.6 Grafana Alerting

Configure Grafana Alert Rules on the `power` panel to send notifications (email, Slack, PagerDuty) when power exceeds 150W. This provides a Grafana-native alert in addition to the Lambda-published MQTT alert.

### 11.7 Historical Analytics

Add a second DynamoDB query pattern: hourly/daily aggregates using Lambda scheduled via EventBridge. Compute peak load time, average daily consumption (kWh), and cost estimation based on Uganda's UMEME tariff rates.

---

*Generated: 2026-04-17 | Author: Caleb | Version 1.0.0*
