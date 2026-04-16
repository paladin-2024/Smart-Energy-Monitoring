# ThingSpeak Setup Guide — Smart Energy Monitor (Part B)

**File:** `Cloud/THINGSPEAK_SETUP.md`
**Author:** Caleb
**Date:** 2026-04-17
**Version:** 1.0.0

---

## Table of Contents

1. [Create a ThingSpeak Account](#1-create-a-thingspeak-account)
2. [Create a New Channel](#2-create-a-new-channel)
3. [Configure Channel Fields](#3-configure-channel-fields)
4. [Get Your API Keys](#4-get-your-api-keys)
5. [Update sketch_wifi.ino](#5-update-sketch_wifiino)
6. [Configure Dashboard Widgets](#6-configure-dashboard-widgets)
7. [Set Up the React Alert (Email on Overload)](#7-set-up-the-react-alert-email-on-overload)
8. [Upload the MATLAB Analysis Script](#8-upload-the-matlab-analysis-script)
9. [Verify Live Data Flow](#9-verify-live-data-flow)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Create a ThingSpeak Account

**Step 1:** Go to [thingspeak.com](https://thingspeak.com) and click **"Get Started For Free"**.

**Step 2:** Click **"Create one!"** under the sign-in form to register a new account.

**Step 3:** Fill in your name, email address, and password. Accept the terms and click **"Continue"**.

**Step 4:** Check your email for a verification link from MathWorks (ThingSpeak is a MathWorks product). Click the link to verify your account.

**Step 5:** Log in at [thingspeak.com](https://thingspeak.com) with your new credentials.

> *Screenshot description: The ThingSpeak dashboard shows a "My Channels" panel with a blue "New Channel" button in the top right.*

---

## 2. Create a New Channel

**Step 1:** From the ThingSpeak dashboard, click **"Channels"** in the top navigation bar, then click **"My Channels"**.

**Step 2:** Click the **"New Channel"** button.

**Step 3:** Fill in the channel details:
- **Name:** `Smart Energy Monitor`
- **Description:** `Real-time voltage, current, and power monitoring with overload detection`

**Step 4:** Do NOT click Save yet — configure the fields first in the next section.

---

## 3. Configure Channel Fields

Still on the New Channel form, scroll down to the **Fields** section.

Enable and name each field exactly as shown:

| Field | Enable | Name |
|---|---|---|
| Field 1 | ✓ checked | `Voltage (V)` |
| Field 2 | ✓ checked | `Current (A)` |
| Field 3 | ✓ checked | `Power (W)` |
| Field 4 | ✓ checked | `Alert Flag` |
| Field 5–8 | Leave unchecked | — |

**Why these field assignments?**
- Field 3 (Power) is the primary analysis field — used by the line graph, gauge, MATLAB script, and React alert.
- Field 4 (Alert Flag) gives the dashboard a binary overload indicator independent of the raw power value.

**Step 5:** Scroll to the bottom and click **"Save Channel"**.

> *Screenshot description: After saving, you are redirected to the channel page showing four empty field charts in a 2×2 grid.*

---

## 4. Get Your API Keys

**Step 1:** On your channel page, click the **"API Keys"** tab.

**Step 2:** Copy the **Write API Key** (looks like `ABCDEF1234567890` — 16 characters). You will paste this into `sketch_wifi.ino`.

**Step 3:** Also copy the **Read API Key** — you will need this for the MATLAB Analysis script.

**Step 4:** Note your **Channel ID** — it is displayed at the top of the channel page as a number (e.g. `2345678`). You need this for the MATLAB script.

> *Screenshot description: The API Keys tab shows two boxes: "Write API Key" and "Read API Key", each with a clipboard copy icon.*

---

## 5. Update sketch_wifi.ino

Open `Cloud/sketch_wifi.ino` in your Arduino IDE or text editor and update the three configuration lines near the top:

```cpp
#define WIFI_SSID              "YourNetworkName"
#define WIFI_PASSWORD          "YourNetworkPassword"
#define THINGSPEAK_API_KEY     "ABCDEF1234567890"   // ← your Write API Key
```

Flash the updated sketch to your ESP32. Open the Serial Monitor at **115200 baud** and confirm you see:

```
Connecting to WiFi 'YourNetworkName'..........
WiFi connected. IP address: 192.168.1.x
Setup complete. Entering main loop.
voltage:220.15,current:0.68,power:149.70,alert:0
ThingSpeak POST → http://api.thingspeak.com/update?api_key=...
ThingSpeak response: 1
```

A response of `1` (or any positive integer) means the first entry was accepted. A response of `0` means the POST was rejected — most commonly because less than 15 seconds passed since the previous upload.

---

## 6. Configure Dashboard Widgets

### Widget 1 — Power Trend Line Graph

**Step 1:** On your channel page, click the **"Private View"** tab, then click **"Add Widgets"**.

**Step 2:** Select **"Charts"** → **"Line Chart"**.

**Step 3:** Configure:
- **Field:** Field 3 (Power W)
- **Title:** `Power Trend`
- **Y-axis label:** `Watts`
- **Y-axis min:** `0`
- **Y-axis max:** `300`
- **Colour:** Blue

**Step 4:** Click **"Save"**.

> *Screenshot description: The line chart widget shows a blue power trend line over time. The x-axis shows timestamps; the y-axis shows Watts 0–300.*

---

### Widget 2 — Live Wattage Gauge

**Step 1:** Click **"Add Widgets"** again.

**Step 2:** Select **"Gauges"** → **"Gauge"**.

**Step 3:** Configure:
- **Field:** Field 3 (Power W)
- **Title:** `Live Wattage`
- **Min value:** `0`
- **Max value:** `300`
- **Colour thresholds:** Green 0–100W, Yellow 100–150W, Red 150–300W

**Step 4:** Click **"Save"**.

---

### Widget 3 — Voltage Numeric Display

**Step 1:** Click **"Add Widgets"** again.

**Step 2:** Select **"Numeric Display"**.

**Step 3:** Configure:
- **Field:** Field 1 (Voltage V)
- **Title:** `Live Voltage`
- **Units:** `V`
- **Decimal places:** `1`

**Step 4:** Click **"Save"**.

> *Screenshot description: The Private View tab now shows three widgets arranged on the dashboard: a line chart spanning the full width, and below it the gauge and numeric display side by side.*

---

## 7. Set Up the React Alert (Email on Overload)

ThingSpeak **React** widgets trigger actions when field values meet a condition — in this case, sending an email when power exceeds 150W.

**Step 1:** In the ThingSpeak top navigation, click **"Apps"** → **"React"**.

**Step 2:** Click **"New React"**.

**Step 3:** Fill in the React settings:

| Setting | Value |
|---|---|
| **React Name** | `Overload Alert` |
| **Condition Type** | Numeric |
| **Test Frequency** | On Data Insertion |
| **Channel** | Smart Energy Monitor |
| **Field** | Field 3 (Power W) |
| **Condition** | greater than |
| **Value** | `150` |
| **Action** | ThingHTTP (see below) |
| **Run action** | Each time condition is met |

**Step 4:** Before you can select ThingHTTP as an action, you need to create a ThingHTTP request:
- Go to **Apps → ThingHTTP → New ThingHTTP**
- **Name:** `Send Overload Email`
- **URL:** `https://api.thingspeak.com/apps/thingtweet/1/tweet` *(or use a webhook to your email service)*

> **Simpler alternative:** ThingSpeak React can send a direct email if your account has email notifications enabled. Check **Apps → Email Notification** and enable it, then select **"Send Email"** as the React action.

**Step 5:** Save the React widget.

> *Screenshot description: The React list page shows "Overload Alert" with a green "Active" badge. The condition column shows "Field 3 > 150".*

---

## 8. Upload the MATLAB Analysis Script

**Step 1:** In the ThingSpeak top navigation, click **"Apps"** → **"MATLAB Analysis"**.

**Step 2:** Click **"New"**.

**Step 3:** Give it a name: `Power Moving Average`.

**Step 4:** In the code editor, paste the entire contents of `Cloud/matlab_analysis.m`.

**Step 5:** Update the two configuration lines at the top of the script:
```matlab
CHANNEL_ID   = 2345678;           % ← your actual channel ID (number, no quotes)
READ_API_KEY = 'YOUR_READ_API_KEY'; % ← your Read API Key
```

**Step 6:** Click **"Save and Run"**.

**Step 7:** The output panel at the bottom will show the summary statistics:
```
--- Power Summary (last 100 readings) ---
Mean power:    142.30 W
Max power:     201.50 W
Min power:      98.20 W
Std deviation:  28.45 W
Moving avg window: 10 readings (150 seconds)
```

**Step 8:** A MATLAB figure window will open showing the power trend with the moving average overlay and any overload event markers.

> *Screenshot description: The MATLAB figure shows a blue jagged raw power line and a smoother red moving average line above it. Red circles mark the two overload events where power exceeded 150W. A dashed black line at 150W marks the threshold.*

**Step 9 (optional):** To run the analysis automatically whenever new data arrives:
- Click **"TimeControl"** on the MATLAB Analysis page
- Set frequency to **"Every 15 minutes"** (or match your upload interval)

---

## 9. Verify Live Data Flow

After completing all steps above, verify the end-to-end flow:

1. **ESP32 Serial Monitor** — confirm `ThingSpeak response: N` (positive integer) every 15 seconds
2. **ThingSpeak Channel → Private View** — confirm all 4 field charts are updating
3. **Gauge widget** — confirm it moves when you turn the potentiometers in Wokwi
4. **Trigger overload** — turn both potentiometers up in Wokwi until power > 150W. Within 15 seconds the gauge should enter the red zone and (if React is configured) an email notification should arrive.
5. **MATLAB Analysis** — click "Save and Run" again after ~10 readings to see the moving average chart.

---

## 10. Troubleshooting

### Error 1: ThingSpeak response is always 0

**Cause:** Updates are being sent faster than ThingSpeak's 15-second minimum interval.

**Fix:**
- Verify `UPLOAD_INTERVAL` in `sketch_wifi.ino` is set to `15000` (15,000 ms).
- If testing rapidly, wait at least 15 seconds between manual resets.
- Check the Serial Monitor — if the timestamp between two "ThingSpeak POST" lines is less than 15 seconds, the timer logic has a bug. Use `millis() - g_lastUpload >= UPLOAD_INTERVAL` (already implemented).

---

### Error 2: WiFi connects but HTTP POST fails (negative response code)

**Cause:** Incorrect API key, wrong URL, or network firewall blocking HTTP on port 80.

**Fix:**
1. Double-check `THINGSPEAK_API_KEY` in `sketch_wifi.ino` — it must be the **Write** API key, not the Read key.
2. Confirm the URL is `http://api.thingspeak.com/update` (HTTP, not HTTPS — some ESP32 HTTPClient setups require extra configuration for HTTPS).
3. Test your network: open a browser on the same WiFi network and navigate to `http://api.thingspeak.com` — if it fails, your network may be blocking port 80.
4. Try switching to the ThingSpeak HTTPS endpoint with `WiFiClientSecure` if your network blocks plain HTTP.

---

### Error 3: MATLAB Analysis shows "Not enough data to plot"

**Cause:** The channel has fewer than 2 readings, or the Channel ID / Read API Key is wrong.

**Fix:**
1. Confirm the ESP32 has been running and posting for at least 30 seconds (2 readings at 15s intervals).
2. On the ThingSpeak channel page → **Private View**, check that the Field 3 chart shows at least one data point.
3. In `matlab_analysis.m`, verify `CHANNEL_ID` is a number (no quotes) and `READ_API_KEY` is a string (in single quotes).
4. Click **"Save and Run"** again — the first run after account creation sometimes times out.
