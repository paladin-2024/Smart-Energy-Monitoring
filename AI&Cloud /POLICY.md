# IoT Policy Reference — Smart Energy Monitor (Part C)

**File:** `AI&Cloud /POLICY.md`
**Author:** Caleb
**Date:** 2026-04-17
**Version:** 1.0.0

---

## Table of Contents

1. [Overview](#1-overview)
2. [Policy Principles](#2-policy-principles)
3. [Permission Breakdown](#3-permission-breakdown)
4. [What Happens If a Permission Is Missing](#4-what-happens-if-a-permission-is-missing)
5. [How to Apply This Policy](#5-how-to-apply-this-policy)
6. [Replacing Placeholders](#6-replacing-placeholders)

---

## 1. Overview

`aws_iot_policy.json` is an AWS IoT Core resource-based policy attached to the
device certificate of the ESP32 Thing. It controls exactly what the device is
allowed to do on the AWS IoT message broker — no more, no less.

AWS IoT Core enforces this policy on every MQTT action the device attempts.
If an action is not explicitly allowed, it is denied and the device receives
an error or disconnection.

---

## 2. Policy Principles

This policy follows the **principle of least privilege**: the device is granted
only the four permissions it genuinely needs to operate. It cannot:
- Delete IoT resources
- Create other Things or certificates
- Access DynamoDB directly (that is Lambda's job)
- Publish to any topic except `energy/monitor`
- Subscribe to any topic except `energy/predictions`

Restricting the policy to specific topic ARNs means a compromised device cannot
be used to poison other topics or eavesdrop on unrelated data streams.

---

## 3. Permission Breakdown

### Statement 1 — `iot:Connect`

```json
"Action": "iot:Connect",
"Resource": "arn:aws:iot:REGION:ACCOUNT:client/esp32-001"
```

**What it does:** Allows the ESP32 to establish a MQTT connection to the
AWS IoT Core broker using the client ID `esp32-001`.

**Why it is needed:** Without `iot:Connect`, the TLS handshake succeeds but the
MQTT CONNECT packet is rejected, and the device immediately disconnects.

**What happens if missing:** The device connects at the TCP/TLS level but is
immediately disconnected with MQTT return code 5 (Not Authorised). The Serial
Monitor shows repeated connection attempts with no success.

**Why the client ID is restricted:** Locking the resource to a specific client ID
(`esp32-001`) means only this device — using this exact name — can connect.
A wildcard (`client/*`) would allow any device to connect as long as it has a
copy of the certificate, which weakens security.

---

### Statement 2 — `iot:Publish`

```json
"Action": "iot:Publish",
"Resource": "arn:aws:iot:REGION:ACCOUNT:topic/energy/monitor"
```

**What it does:** Allows the device to publish MQTT messages to the topic
`energy/monitor`.

**Why it is needed:** This is the topic the ESP32 publishes sensor readings to.
Without this permission, every `mqttClient.publish()` call in `sketch_aws.ino`
silently fails — the message is dropped and never reaches IoT Core or Lambda.

**What happens if missing:** No data arrives in DynamoDB, no Lambda is triggered,
no predictions are generated. The device shows "Publish FAILED" on the Serial
Monitor.

**Why only this topic:** Restricting to `energy/monitor` prevents the device from
injecting data into other topics (e.g. `energy/predictions`), which could spoof
AI predictions.

---

### Statement 3 — `iot:Subscribe`

```json
"Action": "iot:Subscribe",
"Resource": "arn:aws:iot:REGION:ACCOUNT:topicfilter/energy/predictions"
```

**What it does:** Allows the device to send a MQTT SUBSCRIBE packet for the
topic filter `energy/predictions`.

**Why it is needed:** The ESP32 subscribes to `energy/predictions` to receive
AI prediction results published by Lambda. Without this, `mqttClient.subscribe()`
returns false and the device never registers interest in the predictions topic.

**Note the ARN difference:** Subscribe uses `topicfilter/` in the ARN (not
`topic/`) because a subscription is a filter registration, not a message send.

**What happens if missing:** The subscribe call fails silently. The device never
receives prediction messages. `g_predicted` stays at -1 and the "AI:" line never
appears on the OLED.

---

### Statement 4 — `iot:Receive`

```json
"Action": "iot:Receive",
"Resource": "arn:aws:iot:REGION:ACCOUNT:topic/energy/predictions"
```

**What it does:** Allows the device to receive MQTT messages delivered to
the `energy/predictions` topic.

**Why it is needed:** `iot:Subscribe` registers the filter; `iot:Receive` permits
the actual message delivery. Both are required — subscribe without receive means
the subscription is registered but messages are silently dropped before they
reach the device.

**This is a common confusion point:** Many developers add only `iot:Subscribe`
and wonder why no messages arrive. AWS IoT Core requires both permissions.

**What happens if missing:** Subscriptions appear successful, but no messages
are ever delivered to the device. Lambda publishes predictions but they
are filtered out at the broker before reaching the ESP32.

---

## 4. What Happens If a Permission Is Missing

| Missing Permission | Symptom | Error Location |
|---|---|---|
| `iot:Connect` | Device cannot connect; endless reconnect loop | Serial Monitor: MQTT return code 5 |
| `iot:Publish` | No data in DynamoDB; Lambda never triggered | Serial Monitor: "Publish FAILED" |
| `iot:Subscribe` | No prediction subscriptions; "Subscribe failed" message | Serial Monitor |
| `iot:Receive` | Subscribe succeeds but no messages delivered | No visible error — silent failure |

---

## 5. How to Apply This Policy

1. Open `aws_iot_policy.json` and replace all four `YOUR_REGION` and `YOUR_ACCOUNT_ID` placeholders.
2. In AWS Console → IoT Core → Security → Policies → Create policy.
3. Name it `SmartEnergyMonitorPolicy`.
4. Select "JSON" editor and paste the updated policy.
5. Click "Create".
6. Attach the policy to your device certificate:
   - IoT Core → Security → Certificates → your certificate → Policies → Attach policy.

See `AWS_SETUP.md` Section 3 for screenshots and step-by-step instructions.

---

## 6. Replacing Placeholders

| Placeholder | Where to find the value |
|---|---|
| `YOUR_REGION` | AWS Console top-right region selector (e.g. `us-east-1`) |
| `YOUR_ACCOUNT_ID` | AWS Console → top-right account menu → Account ID (12-digit number) |
