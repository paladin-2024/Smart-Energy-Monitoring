
"""
=============================================================================
FILE:        lambda_function.py
PROJECT:     Smart Energy Monitoring System — Part C (AWS Lambda AI)
DESCRIPTION: AWS Lambda function triggered by AWS IoT Core rule on topic
             energy/monitor. Saves each incoming reading to DynamoDB,
             fetches the last 10 readings, applies NumPy linear regression
             to predict the next power value, factors in ambient temperature
             from the Open-Meteo API and a time-of-day multiplier, then
             publishes the AI prediction back to AWS IoT Core on the topic
             energy/predictions.

             Also handles HTTP GET requests (via Lambda Function URL) so
             Grafana Cloud can query historical readings using the Infinity
             data source plugin.

AUTHOR:      Caleb
DATE:        2026-04-17
VERSION:     1.0.0
LICENSE:     MIT

AWS SERVICES USED:
    - AWS IoT Core   : triggers this function via IoT Rule; receives predictions
    - AWS DynamoDB   : stores all sensor readings (table: EnergyReadings)
    - Lambda Function URL : exposes HTTP endpoint for Grafana Infinity plugin

ENVIRONMENT VARIABLES (set in Lambda Console → Configuration → Environment):
    DYNAMODB_TABLE   : EnergyReadings
    IOT_ENDPOINT     : https://YOUR_ENDPOINT.iot.YOUR_REGION.amazonaws.com
    DEVICE_ID        : esp32-001
    OPEN_METEO_LAT   : 0.3163   (Kampala, Uganda latitude)
    OPEN_METEO_LON   : 32.5822  (Kampala, Uganda longitude)
=============================================================================
"""

import json
import os
import logging
import urllib.request
import urllib.error
from datetime import datetime, timezone, timedelta
from decimal import Decimal

import boto3
import numpy as np
from boto3.dynamodb.conditions import Key

# ---------------------------------------------------------------------------
# Logging setup
# Lambda writes all logs to CloudWatch Logs automatically.
# Using the root logger at INFO level so we can trace every step.
# ---------------------------------------------------------------------------
logger = logging.getLogger()
logger.setLevel(logging.INFO)

# ---------------------------------------------------------------------------
# AWS clients (initialised outside handler for warm-start reuse)
# Creating boto3 clients outside the handler means they are reused across
# Lambda invocations that share the same execution environment (warm start).
# This significantly reduces cold-start latency after the first invocation.
# ---------------------------------------------------------------------------
dynamodb = boto3.resource("dynamodb")
iot_data = boto3.client("iot-data", endpoint_url=os.environ.get("IOT_ENDPOINT"))

# ---------------------------------------------------------------------------
# CONSTANTS (from environment variables with sensible defaults)
# ---------------------------------------------------------------------------
TABLE_NAME     = os.environ.get("DYNAMODB_TABLE", "EnergyReadings")
DEVICE_ID      = os.environ.get("DEVICE_ID",      "esp32-001")
OPEN_METEO_LAT = os.environ.get("OPEN_METEO_LAT", "0.3163")   # Kampala latitude
OPEN_METEO_LON = os.environ.get("OPEN_METEO_LON", "32.5822")  # Kampala longitude

POWER_THRESHOLD   = 150.0   # Watts — overload boundary (matches firmware)
TEMP_BASELINE     = 25.0    # °C — temperature above which AC load increases
TEMP_LOAD_FACTOR  = 0.005   # +0.5% power per °C above baseline (heat → more AC)
MIN_READINGS_FOR_REGRESSION = 2   # Minimum readings needed to fit a line

# Peak hours definition (East Africa Time, UTC+2)
# During peak hours the effective threshold is tighter (stricter monitoring)
# Peak hours reflect typical Uganda residential usage patterns:
#   Morning: 06:00–09:00 (breakfast + heating water)
#   Evening: 17:00–21:00 (cooking, lighting, TV)
PEAK_HOURS = [(6, 9), (17, 21)]   # List of (start_hour, end_hour) tuples
PEAK_MULTIPLIER    = 0.85   # Lower threshold during peak (stricter)
OFFPEAK_MULTIPLIER = 1.00   # Normal threshold during off-peak


# ===========================================================================
# MAIN HANDLER
# ===========================================================================

def lambda_handler(event, context):
    """Main Lambda entry point.

    Detects whether the invocation is from an IoT Rule (sensor data) or from
    an HTTP request (Grafana dashboard query) and routes accordingly.

    IoT Rule events have a flat dict with keys like "voltage", "current", etc.
    HTTP events (Lambda Function URL) have a "requestContext" key with
    "http" sub-key containing "method".

    Args:
        event (dict): The Lambda event payload. Structure varies by trigger.
        context (LambdaContext): Runtime information (function name, timeout, etc.)

    Returns:
        dict: For HTTP events — API Gateway-compatible response with statusCode.
              For IoT events — prediction result dict (not returned to caller).
    """
    logger.info("Lambda invoked. Event: %s", json.dumps(event))

    # Detect HTTP invocation (Lambda Function URL or API Gateway)
    # HTTP events always contain a 'requestContext' key with 'http' sub-key
    if "requestContext" in event and "http" in event.get("requestContext", {}):
        return handle_http_request(event)

    # Otherwise treat as an IoT Rule trigger
    return handle_iot_event(event)


# ===========================================================================
# IOT EVENT HANDLER
# ===========================================================================

def handle_iot_event(event):
    """Processes an incoming sensor reading from AWS IoT Core.

    Executes the full AI pipeline:
      1. Save reading to DynamoDB
      2. Query last 10 readings
      3. Fetch ambient temperature from Open-Meteo
      4. Apply time-of-day threshold multiplier
      5. Run NumPy linear regression → predict next power value
      6. Apply temperature correction factor
      7. Publish AI prediction back to IoT Core

    Args:
        event (dict): IoT Rule event. Expected keys:
            voltage   (float): Voltage in Volts
            current   (float): Current in Amps
            power     (float): Power in Watts
            alert     (int):   0=SAFE, 1=OVERLOAD
            timestamp (str):   ISO 8601 UTC string
            deviceId  (str):   Device identifier (optional, defaults to DEVICE_ID)

    Returns:
        dict: Prediction result containing predicted_power and metadata.
    """
    # Extract fields from the IoT message payload
    device_id = event.get("deviceId", DEVICE_ID)
    voltage   = float(event.get("voltage", 0))
    current   = float(event.get("current", 0))
    power     = float(event.get("power",   0))
    alert     = int(event.get("alert",     0))
    timestamp = event.get("timestamp", datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"))

    logger.info("Processing reading: V=%.2f, I=%.2f, P=%.2f, alert=%d", voltage, current, power, alert)

    # ── Step 1: Save incoming reading to DynamoDB ──────────────────────────
    # We save first so this reading is included in the regression query below.
    # The table uses deviceId (PK) + timestamp (SK) as the composite key.
    save_reading(device_id, timestamp, voltage, current, power, alert)

    # ── Step 2: Fetch last 10 readings from DynamoDB ───────────────────────
    # ScanIndexForward=False returns items in descending timestamp order,
    # so the most recent readings come first. We take the first 10.
    readings = get_recent_readings(device_id, limit=10)
    logger.info("Fetched %d recent readings for regression.", len(readings))

    # ── Step 3: Fetch ambient temperature from Open-Meteo ─────────────────
    temperature = get_temperature()
    logger.info("Ambient temperature: %.1f °C", temperature)

    # ── Step 4: Compute time-of-day threshold multiplier ──────────────────
    multiplier = get_time_of_day_multiplier()
    effective_threshold = POWER_THRESHOLD * multiplier
    logger.info("Time-of-day multiplier: %.2f → effective threshold: %.1f W",
                multiplier, effective_threshold)

    # ── Step 5: NumPy linear regression ───────────────────────────────────
    # Linear regression fits a straight line y = mx + b through the last N
    # power readings (y-values), where x is the reading index (0, 1, 2, ...).
    # We then predict the NEXT reading by evaluating the line at x = N.
    # This is the simplest predictive model: it assumes the power trend
    # (rising, falling, or flat) will continue at the same rate.
    predicted_power = run_linear_regression(readings, power)
    logger.info("Linear regression prediction: %.2f W", predicted_power)

    # ── Step 6: Apply temperature correction factor ────────────────────────
    # In Uganda, higher ambient temperature means more air conditioning and
    # fan usage, which increases power load. We add 0.5% per °C above 25°C.
    # Below 25°C there is no correction (factor = 1.0).
    temp_excess   = max(0.0, temperature - TEMP_BASELINE)
    temp_factor   = 1.0 + (temp_excess * TEMP_LOAD_FACTOR)
    adjusted_pred = predicted_power * temp_factor
    logger.info("Temperature correction: +%.1f°C above baseline → factor %.4f → adjusted %.2f W",
                temp_excess, temp_factor, adjusted_pred)

    # ── Step 7: Publish AI prediction back to IoT Core ────────────────────
    prediction_payload = {
        "deviceId":        device_id,
        "predicted_power": round(adjusted_pred, 2),
        "raw_prediction":  round(predicted_power, 2),
        "temperature":     round(temperature, 1),
        "temp_factor":     round(temp_factor, 4),
        "multiplier":      multiplier,
        "threshold":       round(effective_threshold, 1),
        "readings_used":   len(readings),
        "timestamp":       datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    publish_prediction(prediction_payload)

    return prediction_payload


# ===========================================================================
# HTTP REQUEST HANDLER (for Grafana Infinity plugin)
# ===========================================================================

def handle_http_request(event):
    """Handles HTTP GET requests from Grafana Cloud via Lambda Function URL.

    Grafana's Infinity data source plugin queries this endpoint to retrieve
    historical sensor readings for display on the dashboard. Returns the last
    50 readings as a JSON array.

    The Lambda Function URL passes HTTP requests as events with a
    'requestContext.http.method' field. Only GET is supported here.

    Args:
        event (dict): Lambda Function URL event with HTTP context.

    Returns:
        dict: API Gateway-compatible HTTP response:
            statusCode (int): 200 on success, 405 on wrong method, 500 on error
            headers (dict):   Content-Type and CORS headers
            body (str):       JSON-serialised array of readings
    """
    method = event.get("requestContext", {}).get("http", {}).get("method", "GET")
    logger.info("HTTP %s request received for Grafana endpoint.", method)

    # Only allow GET requests — Grafana Infinity uses GET for data queries
    if method != "GET":
        return {
            "statusCode": 405,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": "Method not allowed. Use GET."})
        }

    try:
        # Fetch the last 50 readings for the dashboard time series panel
        readings = get_recent_readings(DEVICE_ID, limit=50)

        # Convert Decimal types (DynamoDB returns these) to float for JSON
        serialisable = [
            {
                "timestamp":       str(r.get("timestamp", "")),
                "voltage":         float(r.get("voltage", 0)),
                "current":         float(r.get("current", 0)),
                "power":           float(r.get("power", 0)),
                "alert":           int(r.get("alert", 0)),
                "predicted_power": float(r.get("predicted_power", 0)),
                "temperature":     float(r.get("temperature", 0)),
            }
            for r in readings
        ]

        return {
            "statusCode": 200,
            "headers": {
                "Content-Type": "application/json",
                # CORS header allows Grafana Cloud (any origin) to query this endpoint
                "Access-Control-Allow-Origin": "*",
            },
            "body": json.dumps(serialisable)
        }

    except Exception as exc:
        logger.error("HTTP handler error: %s", str(exc))
        return {
            "statusCode": 500,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": str(exc)})
        }


# ===========================================================================
# DYNAMODB OPERATIONS
# ===========================================================================

def save_reading(device_id, timestamp, voltage, current, power, alert):
    """Saves a sensor reading to the EnergyReadings DynamoDB table.

    The table uses a composite primary key:
        PK: deviceId  (String) — identifies the physical device
        SK: timestamp (String) — ISO 8601 UTC, enables time-range queries

    Using Decimal for numeric values because DynamoDB does not support Python
    float — it uses its own Decimal type for all number attributes.

    Args:
        device_id (str):  Device identifier (e.g. "esp32-001")
        timestamp (str):  ISO 8601 UTC timestamp string
        voltage   (float): Voltage in Volts
        current   (float): Current in Amps
        power     (float): Power in Watts
        alert     (int):   0 or 1

    Raises:
        botocore.exceptions.ClientError: If the DynamoDB put_item call fails.
    """
    table = dynamodb.Table(TABLE_NAME)

    # boto3 DynamoDB requires numeric values as Decimal, not float
    # str → Decimal conversion avoids floating-point precision issues
    item = {
        "deviceId":  device_id,
        "timestamp": timestamp,
        "voltage":   Decimal(str(round(voltage, 2))),
        "current":   Decimal(str(round(current, 2))),
        "power":     Decimal(str(round(power, 2))),
        "alert":     alert,
    }

    # PutItem writes the item, overwriting any existing item with the same key
    response = table.put_item(Item=item)
    logger.info("DynamoDB put_item response: %s", response.get("ResponseMetadata", {}).get("HTTPStatusCode"))


def get_recent_readings(device_id, limit=10):
    """Queries the last N readings for a device from DynamoDB, newest first.

    Uses a KeyConditionExpression query (not a Scan) so only the relevant
    device's items are read — this is O(log N) rather than O(N) on table size,
    which keeps costs low and latency fast.

    ScanIndexForward=False returns items in descending sort key (timestamp)
    order, so the most recent reading is first in the returned list.

    Args:
        device_id (str): The deviceId partition key value to query.
        limit     (int): Maximum number of readings to return (default 10).

    Returns:
        list[dict]: List of reading dicts, most recent first. Empty list if
                    no readings exist yet.
    """
    table = dynamodb.Table(TABLE_NAME)

    # Query by partition key (deviceId), sort descending by timestamp, take limit
    response = table.query(
        KeyConditionExpression=Key("deviceId").eq(device_id),
        ScanIndexForward=False,   # False = descending timestamp (newest first)
        Limit=limit
    )

    return response.get("Items", [])


# ===========================================================================
# AI: LINEAR REGRESSION
# ===========================================================================

def run_linear_regression(readings, current_power):
    """Predicts the next power value using NumPy linear regression.

    Linear regression explanation (plain English):
        We take the last N power readings and plot them on a graph where:
          - x-axis = reading number (0, 1, 2, ... N-1), newest = N-1
          - y-axis = power value in Watts

        NumPy fits the best straight line through these points using the
        least squares method. "Best" means the line minimises the sum of
        squared vertical distances from each point to the line.

        The line has the equation:  power = slope × reading_number + intercept

        We then predict the NEXT reading (x = N) by plugging it in:
            predicted_power = slope × N + intercept

        If slope is positive, power is trending upward → prediction is higher.
        If slope is negative, power is trending downward → prediction is lower.
        If slope is ~0, power is stable → prediction ≈ current average.

    Why linear regression over a simple moving average?
        A moving average always lags behind a trend. If power has been rising
        for the last 10 readings, a moving average underestimates the next
        value. Linear regression extrapolates the trend, giving a more
        forward-looking prediction — which is the whole point of AI prediction
        in an energy monitoring system.

    Args:
        readings     (list[dict]): Recent readings from DynamoDB (newest first).
                                   Each dict must contain a "power" key.
        current_power (float):     The current power reading, used as fallback
                                   if there are fewer than MIN_READINGS_FOR_REGRESSION.

    Returns:
        float: Predicted next power value in Watts. Never negative (clamped to 0).
    """
    # Extract power values — DynamoDB returns Decimal, convert to float for NumPy
    power_values = [float(r.get("power", 0)) for r in readings]

    # Reverse so oldest reading is at index 0 (chronological order for regression)
    power_values.reverse()

    if len(power_values) < MIN_READINGS_FOR_REGRESSION:
        # Not enough historical data yet — return current reading as best guess
        logger.info("Not enough readings for regression (%d < %d). Using current power.",
                    len(power_values), MIN_READINGS_FOR_REGRESSION)
        return current_power

    # Build x-axis: reading indices [0, 1, 2, ..., N-1]
    x = np.arange(len(power_values), dtype=float)
    y = np.array(power_values, dtype=float)

    # np.polyfit(x, y, 1) fits a degree-1 polynomial (straight line: y = mx + b)
    # Returns [slope (m), intercept (b)] — the parameters of the best-fit line
    coefficients = np.polyfit(x, y, 1)
    slope     = coefficients[0]   # m: rate of change of power per reading
    intercept = coefficients[1]   # b: extrapolated power at reading index 0

    # Predict the next reading: x = N (one step beyond the last observed index)
    next_x = float(len(power_values))
    predicted = slope * next_x + intercept

    logger.info("Regression fit: slope=%.4f W/reading, intercept=%.2f W, predict x=%d → %.2f W",
                slope, intercept, int(next_x), predicted)

    # Clamp to non-negative — power cannot be negative
    return max(0.0, predicted)


# ===========================================================================
# TIME-OF-DAY MULTIPLIER
# ===========================================================================

def get_time_of_day_multiplier():
    """Returns a threshold multiplier based on the current East Africa Time hour.

    During peak hours (morning rush + evening usage) the effective overload
    threshold is reduced (multiplier < 1.0), making the system more sensitive
    to high consumption. This reflects real-world load patterns in Uganda:
    high demand in the morning (water heating, cooking) and evening (lighting,
    cooking, TV) means grid stress is higher and any overload is more critical.

    Peak hours (EAT = UTC+2):
        06:00–09:00 → multiplier 0.85 (threshold = 127.5W)
        17:00–21:00 → multiplier 0.85 (threshold = 127.5W)
    Off-peak:         multiplier 1.00 (threshold = 150.0W)

    Returns:
        float: 0.85 during peak hours, 1.00 during off-peak.
    """
    # Get current hour in East Africa Time (UTC+2)
    eat_tz    = timezone(timedelta(hours=2))
    current_h = datetime.now(eat_tz).hour

    # Check each peak window
    for (start_h, end_h) in PEAK_HOURS:
        if start_h <= current_h < end_h:
            logger.info("Peak hour detected (EAT %02d:xx). Using multiplier %.2f.", current_h, PEAK_MULTIPLIER)
            return PEAK_MULTIPLIER

    logger.info("Off-peak hour (EAT %02d:xx). Using multiplier %.2f.", current_h, OFFPEAK_MULTIPLIER)
    return OFFPEAK_MULTIPLIER


# ===========================================================================
# OPEN-METEO TEMPERATURE
# ===========================================================================

def get_temperature():
    """Fetches the current ambient temperature at Kampala, Uganda from Open-Meteo.

    Open-Meteo is a free weather API that requires no API key. We query the
    current_weather endpoint which returns the current temperature in Celsius.
    Kampala coordinates: latitude=0.3163, longitude=32.5822.

    The temperature is used to apply a small upward correction to the predicted
    power value — higher ambient temperature increases cooling load (fans, AC).

    API endpoint (no key required):
        https://api.open-meteo.com/v1/forecast
        ?latitude=0.3163&longitude=32.5822&current_weather=true

    Response JSON structure:
        {
          "current_weather": {
            "temperature": 24.5,
            "windspeed": 12.3,
            ...
          }
        }

    Returns:
        float: Current temperature in Celsius. Returns TEMP_BASELINE (25.0°C)
               as a safe fallback if the API call fails for any reason.
    """
    url = (
        f"https://api.open-meteo.com/v1/forecast"
        f"?latitude={OPEN_METEO_LAT}"
        f"&longitude={OPEN_METEO_LON}"
        f"&current_weather=true"
    )

    try:
        # urllib.request is used instead of the requests library to avoid
        # adding an external dependency that would require a Lambda layer.
        # urllib is part of the Python standard library.
        with urllib.request.urlopen(url, timeout=5) as response:
            data = json.loads(response.read().decode("utf-8"))
            temperature = float(data["current_weather"]["temperature"])
            logger.info("Open-Meteo temperature: %.1f °C", temperature)
            return temperature

    except urllib.error.URLError as exc:
        # Network error (DNS failure, timeout, etc.)
        logger.warning("Open-Meteo network error: %s. Using baseline %.1f°C.", str(exc), TEMP_BASELINE)
        return TEMP_BASELINE

    except (KeyError, ValueError, json.JSONDecodeError) as exc:
        # Unexpected response structure or parse error
        logger.warning("Open-Meteo parse error: %s. Using baseline %.1f°C.", str(exc), TEMP_BASELINE)
        return TEMP_BASELINE


# ===========================================================================
# PUBLISH PREDICTION TO IOT CORE
# ===========================================================================

def publish_prediction(payload):
    """Publishes the AI prediction result back to the AWS IoT Core MQTT broker.

    The ESP32 subscribes to the energy/predictions topic and displays the
    predicted power on its OLED screen (the "AI:" line).

    The IoT Data Plane client (iot_data) is used for publishing because it
    targets the device-facing data endpoint (port 8883 / HTTPS REST API),
    whereas the control plane client is used for management operations.

    Args:
        payload (dict): Prediction result to publish. Will be JSON-serialised.

    Raises:
        botocore.exceptions.ClientError: If the IoT publish call fails.
                This is caught and logged rather than re-raised so a publish
                failure does not prevent the DynamoDB write from completing.
    """
    topic    = "energy/predictions"
    message  = json.dumps(payload)

    try:
        # publish() sends a message to the specified topic
        # QoS 0 (at most once) is sufficient — missed predictions are not critical
        response = iot_data.publish(
            topic=topic,
            qos=0,
            payload=message.encode("utf-8")
        )
        logger.info("Published prediction to %s: %s", topic, message)
        logger.info("IoT publish HTTP status: %s",
                    response.get("ResponseMetadata", {}).get("HTTPStatusCode"))

    except Exception as exc:
        # Log but do not re-raise — prediction publish failure is non-fatal
        logger.error("Failed to publish prediction to IoT Core: %s", str(exc))
