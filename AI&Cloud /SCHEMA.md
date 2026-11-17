# DynamoDB Schema Reference — Smart Energy Monitor (Part C)

**File:** `AI&Cloud /SCHEMA.md`
**Author:** Caleb
**Date:** 2026-04-17
**Version:** 1.0.0

---

## Table of Contents

1. [Table Overview](#1-table-overview)
2. [Key Design](#2-key-design)
3. [Attributes](#3-attributes)
4. [Billing Mode](#4-billing-mode)
5. [How Lambda Queries the Table](#5-how-lambda-queries-the-table)
6. [Why This Design](#6-why-this-design)
7. [Sample Item](#7-sample-item)

---

## 1. Table Overview

| Property | Value |
|---|---|
| Table Name | `EnergyReadings` |
| Partition Key (PK) | `deviceId` (String) |
| Sort Key (SK) | `timestamp` (String, ISO 8601) |
| Billing Mode | PAY_PER_REQUEST (on-demand) |
| AWS Free Tier | 25 GB storage + 200M requests/month |

---

## 2. Key Design

### Partition Key — `deviceId` (String)

**What it is:** A string that uniquely identifies the physical ESP32 device.
Example: `"esp32-001"`.

**Why `deviceId` as the partition key?**
DynamoDB distributes data across partitions based on the partition key hash.
Using `deviceId` means all readings from a single device are stored in the same
partition — making it fast and cheap to query all readings for a device without
scanning the entire table.

In a multi-device deployment (e.g. monitoring several apartments), each device
has its own `deviceId` and gets its own partition. Lambda can query each device
independently or fan out across multiple partition keys.

**Alternative considered:** Using a fixed string like `"energy"` as the PK would
put all readings in one partition — causing a "hot partition" problem at scale and
making multi-device queries impossible without a filter (full scan).

---

### Sort Key — `timestamp` (String, ISO 8601 UTC)

**What it is:** The UTC timestamp of the reading in ISO 8601 format.
Example: `"2026-04-17T14:32:00Z"`.

**Why `timestamp` as the sort key?**
DynamoDB sorts items within a partition by their sort key. Using a timestamp
sort key means:
1. Items are stored in chronological order within a device's partition.
2. Lambda can query "the last 10 readings" using `ScanIndexForward=False`
   and `Limit=10` — returning the most recent items first in a single,
   efficient query call.
3. Time range queries (`between start and end`) are possible without a scan.

**Why ISO 8601 string rather than a Unix epoch number?**
ISO 8601 strings sort lexicographically in the same order as chronologically —
`"2026-04-17T14:32:00Z"` < `"2026-04-17T14:32:15Z"`. This means DynamoDB's
natural string sort order is also chronological order, no conversion needed.

Unix epoch integers would also work, but ISO 8601 strings are human-readable
in the DynamoDB console and Grafana table panel — useful for debugging.

---

## 3. Attributes

| Attribute | Type | Description |
|---|---|---|
| `deviceId` | String | Partition key — device identifier |
| `timestamp` | String | Sort key — ISO 8601 UTC reading time |
| `voltage` | Number | Voltage reading in Volts (2 decimal places) |
| `current` | Number | Current reading in Amps (2 decimal places) |
| `power` | Number | Computed power in Watts (P = V × I) |
| `alert` | Number | Overload flag: 0 = SAFE, 1 = OVERLOAD |
| `predicted_power` | Number | AI-predicted next power value (added by Lambda after regression) |
| `temperature` | Number | Ambient temperature at prediction time (°C from Open-Meteo) |

**Note:** DynamoDB is schemaless — only the key attributes (`deviceId`,
`timestamp`) are required. All other attributes are optional and added by
Lambda after the initial `put_item` call. In practice, every item will have
all attributes because Lambda writes them during the same invocation.

---

## 4. Billing Mode

**PAY_PER_REQUEST (on-demand)** is used instead of PROVISIONED capacity because:
- No need to estimate read/write capacity units in advance.
- At 15-second publish intervals, the table receives ~4 writes/minute —
  well within the free tier (200M requests/month).
- Scales automatically if the number of devices increases.
- No idle cost when the device is offline.

---

## 5. How Lambda Queries the Table

### Write (save_reading)

```python
table.put_item(Item={
    "deviceId":  "esp32-001",
    "timestamp": "2026-04-17T14:32:00Z",
    "voltage":   Decimal("220.50"),
    "current":   Decimal("0.68"),
    "power":     Decimal("149.94"),
    "alert":     0
})
```

PutItem always writes the full item. If an item with the same PK+SK exists,
it is overwritten — this prevents duplicate readings if Lambda is retried.

### Read (get_recent_readings)

```python
table.query(
    KeyConditionExpression=Key("deviceId").eq("esp32-001"),
    ScanIndexForward=False,   # Newest first (descending timestamp)
    Limit=10
)
```

This query reads only the 10 most recent items for `esp32-001`.
It does NOT scan the whole table — cost is proportional to items read, not
table size. The response includes items sorted newest-first, ready for the
regression calculation.

---

## 6. Why This Design

| Design Question | Decision | Reason |
|---|---|---|
| Why a composite key (PK + SK)? | Enables efficient time-range queries per device | A single-attribute key would require a full table scan to get recent readings |
| Why not a Global Secondary Index (GSI)? | Not needed for this access pattern | All queries are by deviceId first, then timestamp — the base table key covers this |
| Why PAY_PER_REQUEST? | Free tier friendly, no capacity planning | At 4 writes/min, provisioned capacity would be over-specified and more expensive |
| Why ISO 8601 string timestamps? | Lexicographic = chronological, human-readable | Sorts correctly in DynamoDB, readable in console and Grafana |
| Why store predicted_power in the same item? | One query returns both actual and predicted | Simplifies Grafana query — one HTTP call, one table, one item per reading |

---

## 7. Sample Item

```json
{
  "deviceId":       "esp32-001",
  "timestamp":      "2026-04-17T14:32:00Z",
  "voltage":        220.50,
  "current":        0.68,
  "power":          149.94,
  "alert":          0,
  "predicted_power": 153.20,
  "temperature":    26.5
}
```
