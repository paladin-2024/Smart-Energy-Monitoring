/**
 * =============================================================================
 * FILE:        sketch_aws.ino
 * PROJECT:     Smart Energy Monitoring System — Part C (AWS IoT + AI)
 * DESCRIPTION: ESP32 firmware that reads simulated voltage and current sensors,
 *              computes power, drives the OLED display and status indicators,
 *              and publishes JSON telemetry to AWS IoT Core via MQTT over TLS
 *              every 15 seconds. Also subscribes to the AI prediction topic
 *              (energy/predictions) to display predicted power on the OLED.
 *              Includes MQTT keepalive and graceful reconnect logic.
 * AUTHOR:      Caleb
 * DATE:        2026-04-17
 * VERSION:     1.0.0
 * LICENSE:     MIT
 *
 * HARDWARE (same as Part A/B):
 *   - ESP32 WROOM-32
 *   - OLED SSD1306 128×64 I2C (SDA=GPIO21, SCL=GPIO22)
 *   - Potentiometer 1 → GPIO34 (voltage 0–240V)
 *   - Potentiometer 2 → GPIO35 (current 0–10A)
 *   - Green LED → GPIO26 | Red LED → GPIO27 | Buzzer → GPIO25
 *
 * AWS IoT CONFIGURATION:
 *   - MQTT Broker: YOUR_ENDPOINT.iot.YOUR_REGION.amazonaws.com
 *   - Port: 8883 (MQTT over TLS)
 *   - Publish topic:   energy/monitor
 *   - Subscribe topic: energy/predictions
 *
 * JSON PAYLOAD FORMAT (published to energy/monitor):
 *   {
 *     "deviceId":  "esp32-001",
 *     "voltage":   220.5,
 *     "current":   0.68,
 *     "power":     149.9,
 *     "alert":     0,
 *     "timestamp": "2026-04-17T14:32:00Z"
 *   }
 *
 * CERTIFICATES:
 *   Paste your three AWS IoT certificates between the delimiters below.
 *   Download them from AWS IoT Console → Things → Certificates.
 *   See AI&Cloud /AWS_SETUP.md for step-by-step instructions.
 *
 * LIBRARIES REQUIRED (install via Arduino Library Manager):
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - PubSubClient (by Nick O'Leary)
 *   - ArduinoJson (by Benoit Blanchon)
 *   (WiFiClientSecure is built into the ESP32 Arduino core)
 * =============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ---------------------------------------------------------------------------
// USER CONFIGURATION — update all values marked YOUR_ before flashing
// ---------------------------------------------------------------------------

// WiFi credentials
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// AWS IoT Core endpoint — found in AWS Console → IoT Core → Settings
// Format: xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com
#define AWS_IOT_ENDPOINT "YOUR_ENDPOINT.iot.YOUR_REGION.amazonaws.com"

// AWS IoT MQTT port — always 8883 for TLS mutual authentication
#define AWS_IOT_PORT 8883

// MQTT topics
// energy/monitor     — this device publishes sensor readings here
// energy/predictions — Lambda publishes AI predictions here; this device subscribes
#define MQTT_TOPIC_PUBLISH   "energy/monitor"
#define MQTT_TOPIC_SUBSCRIBE "energy/predictions"

// Unique device identifier — must match the IoT Thing name in AWS Console
#define DEVICE_ID "esp32-001"

// How often to publish sensor data (milliseconds)
#define PUBLISH_INTERVAL 15000UL   // 15 seconds

// NTP server for accurate timestamps in the JSON payload
// AWS IoT Core rejects messages with timestamps that are too far from real time
#define NTP_SERVER   "pool.ntp.org"
#define NTP_GMT_OFFSET     7200    // UTC+2 (East Africa Time)
#define NTP_DAYLIGHT_OFFSET 0

// ---------------------------------------------------------------------------
// AWS IoT TLS CERTIFICATES
// Paste each certificate between its R"EOF( ... )EOF" delimiters.
// These three files are downloaded together from AWS Console when you create
// a certificate for your Thing. See AWS_SETUP.md Section 3.
//
// SECURITY WARNING: Never commit real certificates to a public repository.
// The .gitignore file excludes *.pem and *.key — keep originals outside the repo.
// ---------------------------------------------------------------------------

// Amazon Root CA 1 — authenticates the AWS IoT broker to the device
// Download: https://www.amazontrust.com/repository/AmazonRootCA1.pem
const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
YOUR_AMAZON_ROOT_CA_1_CERTIFICATE_HERE
Paste the full contents of AmazonRootCA1.pem between these lines.
-----END CERTIFICATE-----
)EOF";

// Device Certificate — authenticates this ESP32 to AWS IoT Core
// Filename from AWS Console: xxxxxxxxxx-certificate.pem.crt
const char AWS_CERT_CRT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
YOUR_DEVICE_CERTIFICATE_HERE
Paste the full contents of your device certificate (.pem.crt) here.
-----END CERTIFICATE-----
)EOF";

// Device Private Key — used for TLS mutual authentication
// Filename from AWS Console: xxxxxxxxxx-private.pem.key
// NEVER share this key — it uniquely identifies your device
const char AWS_CERT_PRIVATE[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
YOUR_PRIVATE_KEY_HERE
Paste the full contents of your private key (.pem.key) here.
-----END RSA PRIVATE KEY-----
)EOF";

// ---------------------------------------------------------------------------
// DISPLAY CONFIGURATION (identical to Part A/B)
// ---------------------------------------------------------------------------
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64
#define OLED_RESET       -1
#define SCREEN_ADDRESS  0x3C

// ---------------------------------------------------------------------------
// PIN DEFINITIONS (identical to Part A/B)
// ---------------------------------------------------------------------------
#define PIN_VOLTAGE    34
#define PIN_CURRENT    35
#define PIN_LED_GREEN  26
#define PIN_LED_RED    27
#define PIN_BUZZER     25

// ---------------------------------------------------------------------------
// SCALING CONSTANTS (identical to Part A/B)
// ---------------------------------------------------------------------------
#define ADC_MAX          4095.0f
#define MAX_VOLTAGE       240.0f
#define MAX_CURRENT        10.0f
#define POWER_THRESHOLD   150.0f
#define MAX_POWER         300.0f
#define FLASH_INTERVAL    500

// ---------------------------------------------------------------------------
// GLOBAL STATE
// ---------------------------------------------------------------------------
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClientSecure wifiClient;       // TLS-enabled WiFi client for MQTT
PubSubClient     mqttClient(wifiClient);

float         g_voltage      = 0.0f;
float         g_current      = 0.0f;
float         g_power        = 0.0f;
int           g_alert        = 0;
float         g_predicted    = -1.0f;  // AI predicted power (-1 = not yet received)
bool          g_flash        = false;
unsigned long g_lastFlash    = 0;
unsigned long g_lastPublish  = 0;


// ===========================================================================
// MQTT CALLBACK
// ===========================================================================

/**
 * mqttCallback()
 *
 * Invoked by PubSubClient whenever a message arrives on a subscribed topic.
 * We subscribe to energy/predictions so the Lambda AI result can be shown
 * on the OLED alongside the live reading.
 *
 * Expected JSON payload from Lambda:
 *   {"deviceId":"esp32-001","predicted_power":162.3,"timestamp":"..."}
 *
 * @param topic    C-string of the incoming topic name
 * @param payload  Raw byte array of the message body
 * @param length   Length of the payload in bytes
 */
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Null-terminate the payload bytes so we can treat it as a C-string
  char msg[256];
  unsigned int copyLen = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  Serial.print(F("MQTT message on topic ["));
  Serial.print(topic);
  Serial.print(F("]: "));
  Serial.println(msg);

  // Parse JSON — StaticJsonDocument size chosen to fit the prediction payload
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.print(F("JSON parse error: "));
    Serial.println(error.c_str());
    return;
  }

  // Extract the predicted power value from the Lambda response
  if (doc.containsKey("predicted_power")) {
    g_predicted = doc["predicted_power"].as<float>();
    Serial.print(F("AI predicted power: "));
    Serial.print(g_predicted, 1);
    Serial.println(F(" W"));
  }
}


// ===========================================================================
// WiFi FUNCTIONS
// ===========================================================================

/**
 * connectWiFi()
 * Connects to the configured WiFi network, retrying up to 20 times.
 * NTP time sync is performed after connection — required so the JWT/TLS
 * handshake with AWS IoT Core has a valid system clock.
 */
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("Connecting to WiFi"));
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("WiFi connected. IP: "));
    Serial.println(WiFi.localIP());

    // Sync system clock via NTP — AWS IoT Core rejects TLS handshakes
    // if the device clock is more than a few minutes off from real time
    Serial.println(F("Syncing time via NTP..."));
    configTime(NTP_GMT_OFFSET, NTP_DAYLIGHT_OFFSET, NTP_SERVER);
    struct tm timeInfo;
    int ntpRetries = 0;
    while (!getLocalTime(&timeInfo) && ntpRetries < 10) {
      delay(500);
      ntpRetries++;
    }
    if (ntpRetries < 10) {
      Serial.println(F("NTP sync complete."));
    } else {
      Serial.println(F("NTP sync failed — timestamp may be incorrect."));
    }
  } else {
    Serial.println();
    Serial.println(F("WiFi connection failed."));
  }
}


// ===========================================================================
// MQTT FUNCTIONS
// ===========================================================================

/**
 * connectMQTT()
 *
 * Configures the TLS certificates on the WiFiClientSecure client, then
 * connects to the AWS IoT Core MQTT broker using the device ID as the
 * MQTT client ID. Also subscribes to the predictions topic.
 *
 * Why mutual TLS (mTLS)?
 *   AWS IoT Core uses X.509 certificate-based mutual authentication instead
 *   of username/password. Both sides present certificates: the broker proves
 *   it is genuinely AWS (via the CA), and the device proves its identity
 *   (via the device cert + private key). This is more secure than passwords
 *   because a compromised device does not expose credentials for other devices.
 */
void connectMQTT() {
  // Load the three PEM-encoded certificates into the TLS client
  // These were pasted into the PROGMEM char arrays above
  wifiClient.setCACert(AWS_CERT_CA);
  wifiClient.setCertificate(AWS_CERT_CRT);
  wifiClient.setPrivateKey(AWS_CERT_PRIVATE);

  // Configure MQTT broker address and port
  mqttClient.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);

  // Register the callback for incoming messages on subscribed topics
  mqttClient.setCallback(mqttCallback);

  // Set MQTT keep-alive interval (seconds) — how often the client sends a
  // PINGREQ to keep the broker connection alive when no data is flowing
  mqttClient.setKeepAlive(60);

  // Attempt connection — DEVICE_ID is used as the MQTT client ID
  // It must match the clientId allowed by the IoT policy (aws_iot_policy.json)
  Serial.print(F("Connecting to AWS IoT Core..."));
  while (!mqttClient.connect(DEVICE_ID)) {
    Serial.print(F("."));
    delay(1000);
  }
  Serial.println(F(" connected."));

  // Subscribe to the AI predictions topic
  // Lambda publishes here after computing the linear regression result
  if (mqttClient.subscribe(MQTT_TOPIC_SUBSCRIBE)) {
    Serial.print(F("Subscribed to: "));
    Serial.println(MQTT_TOPIC_SUBSCRIBE);
  } else {
    Serial.println(F("Subscribe failed — check IoT policy permissions."));
  }
}


/**
 * checkMQTTReconnect()
 *
 * Checks MQTT connection health each loop iteration. If the broker
 * connection was dropped (network glitch, AWS-side timeout), this
 * function reconnects and re-subscribes automatically.
 *
 * Why re-subscribe after reconnect?
 *   MQTT subscriptions are not persistent across disconnections in the
 *   default QoS 0 / clean session mode used here. After reconnect,
 *   all subscriptions must be re-established.
 */
void checkMQTTReconnect() {
  if (!mqttClient.connected()) {
    Serial.println(F("MQTT disconnected. Reconnecting..."));

    // Ensure WiFi is still up before attempting MQTT reconnect
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      delay(100);
      connectWiFi();
    }

    // Attempt MQTT reconnect — up to 5 attempts with 1s delay
    int attempts = 0;
    while (!mqttClient.connect(DEVICE_ID) && attempts < 5) {
      delay(1000);
      attempts++;
      Serial.print(F("."));
    }

    if (mqttClient.connected()) {
      Serial.println(F("MQTT reconnected."));
      // Re-subscribe to predictions topic after reconnect
      mqttClient.subscribe(MQTT_TOPIC_SUBSCRIBE);
    } else {
      Serial.println(F("MQTT reconnect failed. Will retry next cycle."));
    }
  }
}


// ===========================================================================
// TIMESTAMP
// ===========================================================================

/**
 * getTimestamp()
 *
 * Returns the current UTC time as an ISO 8601 string.
 * Example: "2026-04-17T14:32:00Z"
 *
 * AWS IoT Core and DynamoDB both use ISO 8601 timestamps as the standard
 * time format. The sort key in DynamoDB is this timestamp, which allows
 * Lambda to query readings in chronological order efficiently.
 *
 * @param buf    Output char buffer to write the timestamp string into
 * @param bufLen Length of the output buffer (must be >= 21 characters)
 */
void getTimestamp(char* buf, size_t bufLen) {
  struct tm timeInfo;
  if (getLocalTime(&timeInfo)) {
    // Format: YYYY-MM-DDTHH:MM:SSZ  (20 chars + null terminator)
    strftime(buf, bufLen, "%Y-%m-%dT%H:%M:%SZ", &timeInfo);
  } else {
    // Fallback if NTP failed — use a placeholder so the payload is still valid
    strncpy(buf, "1970-01-01T00:00:00Z", bufLen);
  }
}


// ===========================================================================
// PUBLISH SENSOR DATA
// ===========================================================================

/**
 * publishSensorData()
 *
 * Builds a JSON payload containing all sensor readings plus a timestamp
 * and publishes it to the energy/monitor MQTT topic.
 *
 * JSON payload structure:
 * {
 *   "deviceId":  "esp32-001",   — identifies this device in DynamoDB (PK)
 *   "voltage":   220.5,          — scaled voltage in Volts
 *   "current":   0.68,           — scaled current in Amps
 *   "power":     149.9,          — computed power in Watts (P = V × I)
 *   "alert":     0,              — 0 = SAFE, 1 = OVERLOAD
 *   "timestamp": "2026-04-17T14:32:00Z"  — ISO 8601 UTC (DynamoDB sort key)
 * }
 *
 * PubSubClient.publish() sends the serialised JSON as a retained=false,
 * QoS 0 MQTT message. QoS 0 (at most once) is sufficient here because
 * missing a single sensor reading is acceptable — data is sent every 15s.
 */
void publishSensorData() {
  // Build the JSON document — 256 bytes is sufficient for this payload
  StaticJsonDocument<256> doc;
  doc["deviceId"] = DEVICE_ID;
  doc["voltage"]  = round(g_voltage * 100.0f) / 100.0f;  // 2 decimal places
  doc["current"]  = round(g_current * 100.0f) / 100.0f;
  doc["power"]    = round(g_power   * 100.0f) / 100.0f;
  doc["alert"]    = g_alert;

  // Add ISO 8601 timestamp
  char timestamp[21];
  getTimestamp(timestamp, sizeof(timestamp));
  doc["timestamp"] = timestamp;

  // Serialise JSON to a char buffer for MQTT publish
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  Serial.print(F("Publishing → "));
  Serial.println(jsonBuffer);

  // Publish to AWS IoT Core
  // mqttClient.publish(topic, payload) returns true on success
  if (mqttClient.publish(MQTT_TOPIC_PUBLISH, jsonBuffer)) {
    Serial.println(F("Publish OK"));
  } else {
    Serial.println(F("Publish FAILED — check broker connection and policy"));
  }
}


// ===========================================================================
// SENSOR + DISPLAY FUNCTIONS (same as Part A/B)
// ===========================================================================

void readSensors() {
  int rawV = analogRead(PIN_VOLTAGE);
  int rawI = analogRead(PIN_CURRENT);
  g_voltage = (rawV / ADC_MAX) * MAX_VOLTAGE;
  g_current = (rawI / ADC_MAX) * MAX_CURRENT;
  g_power   = g_voltage * g_current;
  g_alert   = (g_power > POWER_THRESHOLD) ? 1 : 0;
}

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

void drawBarGraph(float powerVal) {
  const int barX = 14, barY = 54, barWidth = 96, barHeight = 7;
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  int fill = (int)((powerVal / MAX_POWER) * (float)(barWidth - 2));
  if (fill < 0) fill = 0;
  if (fill > barWidth - 2) fill = barWidth - 2;
  if (fill > 0)
    display.fillRect(barX + 1, barY + 1, fill, barHeight - 2, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,   barY); display.print(F("0"));
  display.setCursor(112, barY); display.print(F("3C"));
}

/**
 * updateDisplay()
 * Same as Part A/B but adds a "Pred:" line showing the AI-predicted power
 * received from Lambda via the energy/predictions MQTT topic.
 */
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setTextSize(1);
  display.setCursor(4, 0);
  display.print(F("SMART ENERGY MONITOR"));
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Metrics row
  display.setCursor(0,  12); display.print(F("V:")); display.print(g_voltage, 1);
  display.setCursor(44, 12); display.print(F("I:")); display.print(g_current, 2);
  display.setCursor(90, 12); display.print(F("P:")); display.print((int)g_power);

  // Large power value (centred)
  display.setTextSize(2);
  String powerStr = String(g_power, 1) + "W";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(powerStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 22);
  display.print(powerStr);

  // AI prediction (small, below power number) — only shown once Lambda responds
  display.setTextSize(1);
  if (g_predicted >= 0) {
    display.setCursor(0, 36);
    display.print(F("AI:"));
    display.print(g_predicted, 1);
    display.print(F("W"));
  }

  // Status
  if (g_alert) {
    if (millis() - g_lastFlash >= FLASH_INTERVAL) {
      g_flash = !g_flash;
      g_lastFlash = millis();
    }
    if (g_flash) {
      display.fillRect(10, 44, 108, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(12, 45); display.print(F("!! OVERLOAD !!"));
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(12, 45); display.print(F("!! OVERLOAD !!"));
    }
  } else {
    display.setCursor(20, 44); display.print(F("STATUS: SAFE"));
  }

  drawBarGraph(g_power);
  display.display();
}

void sendSerial() {
  Serial.print(F("voltage:")); Serial.print(g_voltage, 2);
  Serial.print(F(",current:")); Serial.print(g_current, 2);
  Serial.print(F(",power:")); Serial.print(g_power, 2);
  Serial.print(F(",alert:")); Serial.println(g_alert);
}


// ===========================================================================
// SETUP + LOOP
// ===========================================================================

/**
 * setup()
 * Initialises all hardware, connects to WiFi, syncs NTP time, and
 * establishes the MQTT connection to AWS IoT Core.
 */
void setup() {
  Serial.begin(115200);
  Serial.println(F("Smart Energy Monitor v1.0 — Part C (AWS IoT + AI)"));

  pinMode(PIN_LED_GREEN, OUTPUT); digitalWrite(PIN_LED_GREEN, LOW);
  pinMode(PIN_LED_RED,   OUTPUT); digitalWrite(PIN_LED_RED,   LOW);
  pinMode(PIN_BUZZER,    OUTPUT); digitalWrite(PIN_BUZZER,    LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("ERROR: SSD1306 not found."));
    while (true) { delay(1000); }
  }

  // Splash
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4,  8); display.println(F("SMART ENERGY MONITOR"));
  display.setCursor(10, 24); display.println(F("Connecting WiFi..."));
  display.display();

  connectWiFi();

  display.clearDisplay();
  display.setCursor(4,  8); display.println(F("SMART ENERGY MONITOR"));
  display.setCursor(10, 24); display.println(F("Connecting AWS IoT.."));
  display.display();

  connectMQTT();

  display.clearDisplay();
  display.setCursor(4,  8); display.println(F("SMART ENERGY MONITOR"));
  display.setCursor(10, 24); display.println(F("AWS IoT: Connected"));
  display.setCursor(10, 38); display.println(F("Starting monitor..."));
  display.display();
  delay(1500);

  Serial.println(F("Setup complete. Publishing to: ") );
  Serial.println(MQTT_TOPIC_PUBLISH);
}


/**
 * loop()
 *
 * Main execution loop. Each iteration:
 *   1. Calls mqttClient.loop() — processes incoming MQTT messages (predictions)
 *      and sends MQTT keepalive PINGREQs. MUST be called regularly.
 *   2. Checks MQTT connection health and reconnects if needed
 *   3. Reads sensors and computes power
 *   4. Updates LEDs and buzzer
 *   5. Redraws OLED (including AI prediction if received)
 *   6. Outputs serial data
 *   7. Publishes JSON to AWS IoT Core every 15 seconds
 *   8. Waits 500ms
 */
void loop() {
  // Process incoming MQTT messages and maintain keepalive
  // This MUST be called every loop iteration — PubSubClient requires it
  mqttClient.loop();

  checkMQTTReconnect();
  readSensors();
  updateIndicators();
  updateDisplay();
  sendSerial();

  // Publish to AWS IoT Core every PUBLISH_INTERVAL ms
  if (millis() - g_lastPublish >= PUBLISH_INTERVAL) {
    publishSensorData();
    g_lastPublish = millis();
  }

  delay(500);
}
