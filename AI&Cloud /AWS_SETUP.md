# AWS Setup Guide — Smart Energy Monitor (Part C)

**File:** `AI&Cloud /AWS_SETUP.md`
**Author:** Caleb
**Date:** 2026-04-17
**Version:** 1.0.0

---

## Table of Contents

1. [Create an AWS Account](#1-create-an-aws-account)
2. [Create an IoT Thing and Download Certificates](#2-create-an-iot-thing-and-download-certificates)
3. [Create and Attach the IoT Policy](#3-create-and-attach-the-iot-policy)
4. [Create the DynamoDB Table](#4-create-the-dynamodb-table)
5. [Deploy the Lambda Function](#5-deploy-the-lambda-function)
6. [Create the IoT Rule](#6-create-the-iot-rule)
7. [Set Up Grafana Cloud](#7-set-up-grafana-cloud)
8. [Flash sketch_aws.ino to the ESP32](#8-flash-sketch_awsino-to-the-esp32)
9. [End-to-End Test](#9-end-to-end-test)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Create an AWS Account

**Step 1:** Go to [aws.amazon.com](https://aws.amazon.com) and click **"Create an AWS Account"**.

**Step 2:** Enter your email address and choose an account name (e.g. "SmartEnergyMonitor").

**Step 3:** Choose **"Personal"** account type. Fill in your contact details.

**Step 4:** Enter a credit card. AWS Free Tier covers all services used in this project:
- IoT Core: 250,000 messages/month free
- Lambda: 1,000,000 invocations/month free
- DynamoDB: 25GB storage + 200M requests/month free

No charges will occur at student project scale.

**Step 5:** Complete phone verification and select the **Free** support plan.

**Step 6:** Once logged in, choose your region using the top-right dropdown.
Recommended: **us-east-1 (N. Virginia)** or **eu-west-1 (Ireland)** — best IoT Core support.

> *Screenshot description: The AWS Management Console home page shows the service search bar at the top and recently visited services below.*

---

## 2. Create an IoT Thing and Download Certificates

**Step 1:** In the AWS search bar, type **"IoT Core"** and open the service.

**Step 2:** In the left sidebar, click **"All devices"** → **"Things"** → **"Create things"**.

**Step 3:** Select **"Create single thing"** → click **"Next"**.

**Step 4:** Set the Thing name to exactly: `esp32-001`
> The name must match `DEVICE_ID` in `sketch_aws.ino` and the client ID in `aws_iot_policy.json`.

**Step 5:** Leave all other settings as default → click **"Next"**.

**Step 6:** On the "Configure device certificate" screen, select **"Auto-generate a new certificate"** → click **"Next"**.

**Step 7:** On the "Attach policies" screen — skip for now, click **"Create thing"**.

**Step 8:** The certificate download screen appears. **Download all four files immediately** — you cannot download the private key again after closing this screen.

| File | What it is | Where to use it |
|---|---|---|
| `xxxxxxxxxx-certificate.pem.crt` | Device certificate | `AWS_CERT_CRT` in sketch_aws.ino |
| `xxxxxxxxxx-private.pem.key` | Device private key | `AWS_CERT_PRIVATE` in sketch_aws.ino |
| `xxxxxxxxxx-public.pem.key` | Device public key | Not used in firmware |
| `AmazonRootCA1.pem` | Amazon root CA | `AWS_CERT_CA` in sketch_aws.ino |

> *Screenshot description: Four download buttons appear. Each has a filename starting with a 10-character alphanumeric prefix. Click all four download buttons and save the files securely.*

**Step 9:** Also click **"Activate"** to activate the certificate (it is inactive by default).

**Step 10:** Click **"Done"**.

---

## 3. Create and Attach the IoT Policy

**Step 1:** In IoT Core left sidebar → **"Security"** → **"Policies"** → **"Create policy"**.

**Step 2:** Name: `SmartEnergyMonitorPolicy`

**Step 3:** Click **"JSON"** to switch to the JSON editor.

**Step 4:** Open `AI&Cloud /aws_iot_policy.json` from this repository. Replace the two placeholders:
- `YOUR_REGION` → your AWS region code (e.g. `us-east-1`)
- `YOUR_ACCOUNT_ID` → your 12-digit AWS account ID (found under your account name in the top-right menu)

**Step 5:** Paste the updated JSON into the policy editor → click **"Create"**.

**Step 6:** Now attach the policy to your certificate:
- Left sidebar → **"Security"** → **"Certificates"**
- Click on your certificate (starts with the 10-char prefix)
- Click **"Actions"** → **"Attach policy"**
- Select `SmartEnergyMonitorPolicy` → click **"Attach policies"**

> *Screenshot description: The certificate detail page shows two tabs: "Things" and "Policies". After attaching, the Policies tab shows "SmartEnergyMonitorPolicy" with a green Active badge.*

**Step 7:** Also attach the Thing to the certificate:
- On the same certificate page → click **"Actions"** → **"Attach to thing"**
- Select `esp32-001` → click **"Attach"**

**Step 8:** Get your IoT Core endpoint URL:
- Left sidebar → **"Settings"**
- Under **"Device data endpoint"**, copy the endpoint URL
- Format: `xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com`
- You will paste this into `sketch_aws.ino` as `AWS_IOT_ENDPOINT`

---

## 4. Create the DynamoDB Table

**Step 1:** In the AWS search bar, type **"DynamoDB"** → open the service.

**Step 2:** Click **"Create table"**.

**Step 3:** Fill in:
- **Table name:** `EnergyReadings`
- **Partition key:** `deviceId` (String)
- **Sort key:** `timestamp` (String)

**Step 4:** Under **"Table settings"**, choose **"Customize settings"**.

**Step 5:** Under **"Read/write capacity settings"**, select **"On-demand"** (PAY_PER_REQUEST) — stays within free tier.

**Step 6:** Leave all other settings as default → click **"Create table"**.

> *Screenshot description: The DynamoDB Tables list shows "EnergyReadings" with status "Active" and billing mode "On-demand".*

**Step 7:** Wait ~30 seconds for the table status to change from "Creating" to "Active".

---

## 5. Deploy the Lambda Function

### 5a. Prepare the Deployment Package

The Lambda function requires `numpy` which is not included in the default Lambda runtime. We package it as a deployment zip.

**On your local machine, run:**
```bash
mkdir lambda_package
cd lambda_package
pip install numpy -t .
cp "/path/to/AI&Cloud /lambda_function.py" .
zip -r ../lambda.zip .
cd ..
```

> On Windows, use 7-Zip or the built-in compress tool instead of `zip`.

### 5b. Create the Lambda Function

**Step 1:** In the AWS search bar, type **"Lambda"** → open the service.

**Step 2:** Click **"Create function"** → **"Author from scratch"**.

**Step 3:** Configure:
- **Function name:** `SmartEnergyMonitor`
- **Runtime:** Python 3.11
- **Architecture:** x86_64

**Step 4:** Under **"Permissions"** → **"Change default execution role"** → **"Create a new role with basic Lambda permissions"**. Note the role name — you will add policies to it next.

**Step 5:** Click **"Create function"**.

### 5c. Upload the Deployment Package

**Step 1:** On the function page, click **"Upload from"** → **".zip file"**.

**Step 2:** Upload the `lambda.zip` you created above → click **"Save"**.

### 5d. Set Environment Variables

**Step 1:** Click **"Configuration"** tab → **"Environment variables"** → **"Edit"**.

**Step 2:** Add the following variables:

| Key | Value |
|---|---|
| `DYNAMODB_TABLE` | `EnergyReadings` |
| `IOT_ENDPOINT` | `https://YOUR_ENDPOINT.iot.YOUR_REGION.amazonaws.com` |
| `DEVICE_ID` | `esp32-001` |
| `OPEN_METEO_LAT` | `0.3163` |
| `OPEN_METEO_LON` | `32.5822` |

**Step 3:** Click **"Save"**.

### 5e. Grant Lambda Permissions

Lambda needs permission to write to DynamoDB and publish to IoT Core.

**Step 1:** Go to **IAM Console** → **"Roles"** → find the Lambda execution role (created in Step 4).

**Step 2:** Click **"Add permissions"** → **"Attach policies"**.

**Step 3:** Search for and attach:
- `AmazonDynamoDBFullAccess`
- `AWSIoTDataAccess`

**Step 4:** Click **"Add permissions"**.

### 5f. Enable Lambda Function URL (for Grafana)

**Step 1:** On the Lambda function page → **"Configuration"** tab → **"Function URL"** → **"Create function URL"**.

**Step 2:** Auth type: **"NONE"** (Grafana needs unauthenticated GET access).

**Step 3:** Enable **"Configure cross-origin resource sharing (CORS)"** → Allow origin: `*`.

**Step 4:** Click **"Save"**.

**Step 5:** Copy the Function URL — format: `https://xxxxxxxxxxxxxxxx.lambda-url.us-east-1.on.aws/`. You will paste this into Grafana.

### 5g. Increase Lambda Timeout

The function calls Open-Meteo externally, which adds latency.

**Step 1:** **"Configuration"** tab → **"General configuration"** → **"Edit"**.

**Step 2:** Set **"Timeout"** to **30 seconds** → click **"Save"**.

---

## 6. Create the IoT Rule

The IoT Rule connects incoming MQTT messages to the Lambda function.

**Step 1:** In IoT Core left sidebar → **"Message routing"** → **"Rules"** → **"Create rule"**.

**Step 2:** Rule name: `EnergyMonitorRule`

**Step 3:** SQL statement:
```sql
SELECT * FROM 'energy/monitor'
```

This passes every field from the MQTT JSON payload to Lambda as the event object.

**Step 4:** Under **"Rule actions"** → **"Add action"** → **"Lambda"**.

**Step 5:** Select your Lambda function `SmartEnergyMonitor` → click **"Add action"**.

**Step 6:** Click **"Create rule"**.

> *Screenshot description: The Rules list shows "EnergyMonitorRule" with status "Enabled" and action "Lambda: SmartEnergyMonitor".*

---

## 7. Set Up Grafana Cloud

**Step 1:** Go to [grafana.com](https://grafana.com) → click **"Create free account"**.

**Step 2:** Sign up and verify your email. The free tier includes a permanent Grafana Cloud instance.

**Step 3:** Log in. You will be on your Grafana Cloud stack dashboard.

### 7a. Install the Infinity Data Source Plugin

**Step 1:** In the left sidebar → **"Connections"** → **"Add new connection"**.

**Step 2:** Search for **"Infinity"** → click on it → click **"Install"**.

**Step 3:** After installation → click **"Add new data source"** → configure:
- **Name:** `LambdaEnergyData`
- Leave all other settings as default
- Click **"Save & test"** — should show "Data source connected"

### 7b. Create the Dashboard

**Step 1:** Left sidebar → **"Dashboards"** → **"New"** → **"New dashboard"**.

**Step 2:** Click **"Add visualization"** → select **"LambdaEnergyData"** (Infinity).

**Step 3:** Configure the panel:
- **Type:** Time series
- **URL:** paste your Lambda Function URL
- **Format:** JSON
- **Root/Path:** leave empty
- **Columns:** add `timestamp`, `power`, `predicted_power`
- **Panel title:** `Power + AI Prediction`
- Click **"Apply"**

**Step 4:** Add a second panel:
- **"Add visualization"** → select **"LambdaEnergyData"**
- **Type:** Table
- **URL:** same Lambda Function URL
- **Columns:** `timestamp`, `voltage`, `current`, `power`, `alert`, `predicted_power`
- **Panel title:** `Sensor Readings`
- Click **"Apply"**

**Step 5:** Click **"Save dashboard"** → name it `Smart Energy Monitor`.

> *Screenshot description: The Grafana dashboard shows two panels. The top panel is a time series chart with a blue "power" line and a purple dashed "predicted_power" line. A red horizontal line at 150 marks the threshold. The bottom panel is a table with columns for each sensor reading.*

---

## 8. Flash sketch_aws.ino to the ESP32

**Step 1:** Open Arduino IDE. Install the following libraries via Library Manager:
- `PubSubClient` by Nick O'Leary
- `ArduinoJson` by Benoit Blanchon
- `Adafruit SSD1306`
- `Adafruit GFX Library`

**Step 2:** Open `AI&Cloud /sketch_aws.ino`.

**Step 3:** Update the configuration section:
```cpp
#define WIFI_SSID        "YourWiFiNetwork"
#define WIFI_PASSWORD    "YourWiFiPassword"
#define AWS_IOT_ENDPOINT "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com"
```

**Step 4:** Paste the three certificate files into the PROGMEM char arrays:
- Open `AmazonRootCA1.pem` → copy all content → paste into `AWS_CERT_CA[]`
- Open `xxxxxxxxxx-certificate.pem.crt` → paste into `AWS_CERT_CRT[]`
- Open `xxxxxxxxxx-private.pem.key` → paste into `AWS_CERT_PRIVATE[]`

**Step 5:** Select your ESP32 board: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**.

**Step 6:** Select the correct COM/USB port → click **"Upload"**.

**Step 7:** Open Serial Monitor at 115200 baud. You should see:
```
Smart Energy Monitor v1.0 — Part C (AWS IoT + AI)
Connecting to WiFi.........
WiFi connected. IP: 192.168.1.x
Syncing time via NTP...
NTP sync complete.
Connecting to AWS IoT Core... connected.
Subscribed to: energy/predictions
Publishing → {"deviceId":"esp32-001","voltage":120.5,"current":0.50,"power":60.2,"alert":0,"timestamp":"2026-04-17T14:32:00Z"}
Publish OK
```

---

## 9. End-to-End Test

Follow these steps to verify the full pipeline is working:

**Step 1 — Publish arrives at IoT Core:**
- In AWS Console → IoT Core → **"MQTT test client"**
- Subscribe to topic: `energy/monitor`
- Press Wokwi "Start Simulation" — within 15 seconds a JSON message should appear

**Step 2 — Lambda is triggered:**
- AWS Console → Lambda → `SmartEnergyMonitor` → **"Monitor"** tab
- "Invocations" count should increment every 15 seconds
- Click **"View CloudWatch logs"** → open the latest log stream to see Lambda output

**Step 3 — DynamoDB has data:**
- AWS Console → DynamoDB → Tables → `EnergyReadings` → **"Explore table items"**
- Items should appear with `deviceId="esp32-001"` and ISO 8601 timestamps

**Step 4 — Predictions published:**
- In IoT Core MQTT test client, subscribe to `energy/predictions`
- After Lambda runs (needs 2+ readings for regression), prediction JSON should appear

**Step 5 — Grafana shows data:**
- Open your Grafana dashboard
- Click the refresh button — both panels should populate
- The time series panel should show live power and AI prediction overlay

**Step 6 — Trigger overload:**
- In Wokwi, turn both potentiometers to maximum
- Within 15 seconds, `alert=1` appears in DynamoDB
- The Grafana table panel shows the alert column change to 1

---

## 10. Troubleshooting

### Error 1: MQTT connect fails / endless reconnect loop

**Cause:** Certificates are incorrect, endpoint is wrong, or the IoT policy is missing.

**Fix:**
1. Double-check `AWS_IOT_ENDPOINT` — it must end in `.amazonaws.com`, no `https://` prefix.
2. Verify the certificate and private key are pasted correctly — they must include the `-----BEGIN-----` and `-----END-----` header/footer lines.
3. In AWS IoT Console → Security → Certificates — confirm the certificate is **Active** (not Inactive or Pending).
4. Confirm the policy `SmartEnergyMonitorPolicy` is attached to the certificate.
5. Confirm the Thing `esp32-001` is attached to the certificate.
6. Check that your WiFi network allows outbound TCP on port 8883 — some corporate/school networks block this port.

---

### Error 2: Lambda is triggered but DynamoDB has no data

**Cause:** Lambda execution role lacks DynamoDB permissions, or `DYNAMODB_TABLE` environment variable is wrong.

**Fix:**
1. In Lambda → Configuration → Environment variables — confirm `DYNAMODB_TABLE` is exactly `EnergyReadings` (case-sensitive).
2. In IAM → Roles → your Lambda role → Permissions — confirm `AmazonDynamoDBFullAccess` is attached.
3. In CloudWatch Logs for the Lambda function, look for error messages like `AccessDeniedException` or `ResourceNotFoundException`. These pinpoint the exact permission or table name issue.
4. Confirm the DynamoDB table is in the same AWS region as the Lambda function.

---

### Error 3: Grafana shows "No data" / panels are empty

**Cause:** Lambda Function URL is not configured, CORS is not enabled, or Infinity plugin configuration is wrong.

**Fix:**
1. Open the Lambda Function URL directly in a browser (GET request) — it should return a JSON array. If it returns an error, check Lambda CloudWatch logs.
2. In Grafana → Data sources → LambdaEnergyData → paste the Function URL and click "Save & test".
3. Ensure the Function URL auth type is **NONE** — authenticated URLs require additional headers that Grafana Infinity does not send by default.
4. In the panel query configuration, set **Format** to **JSON** and verify the column names match the Lambda response keys (`timestamp`, `power`, `predicted_power`).
5. If data exists in DynamoDB but Lambda returns an empty array, confirm there are readings for `DEVICE_ID="esp32-001"` — check the environment variable matches exactly.
