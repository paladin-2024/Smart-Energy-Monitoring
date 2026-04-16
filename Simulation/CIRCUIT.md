# Circuit Reference — Smart Energy Monitor (Part A)

**File:** `Simulation/CIRCUIT.md`
**Author:** Caleb
**Date:** 2026-04-17
**Version:** 1.0.0

---

## Table of Contents

1. [Overview](#1-overview)
2. [Component List](#2-component-list)
3. [Pin Connection Table](#3-pin-connection-table)
4. [Component Details](#4-component-details)
5. [Circuit Design Decisions](#5-circuit-design-decisions)
6. [Wiring Diagram (Text)](#6-wiring-diagram-text)

---

## 1. Overview

This circuit simulates a residential electricity monitoring system using an ESP32 as the central microcontroller. Two potentiometers stand in for an ACS712 current sensor and a resistive voltage divider respectively. The computed power drives an OLED display, status LEDs, and a buzzer — all implemented as a Wokwi simulation so the design can be tested without physical hardware.

---

## 2. Component List

| # | Component | Quantity | Role |
|---|---|---|---|
| 1 | ESP32 WROOM-32 DevKit V1 | 1 | Main microcontroller — reads ADC, drives I2C, controls outputs |
| 2 | OLED SSD1306 128×64 I2C | 1 | Real-time display of voltage, current, power, status, bar graph |
| 3 | Potentiometer 10kΩ | 2 | Simulate ACS712 current sensor (0–10A) and voltage divider (0–240V) |
| 4 | LED — Green 5mm | 1 | Visual SAFE indicator (power ≤ 150W) |
| 5 | LED — Red 5mm | 1 | Visual OVERLOAD indicator (power > 150W) |
| 6 | Resistor 220Ω | 2 | Current limiting for each LED (prevents > 20mA through LED) |
| 7 | Active Buzzer 5V | 1 | Audible alert during OVERLOAD state |

---

## 3. Pin Connection Table

| ESP32 Pin | Connected To | Wire Colour | Purpose |
|---|---|---|---|
| GPIO21 (SDA) | OLED SDA | Green | I2C data line |
| GPIO22 (SCL) | OLED SCL | Blue | I2C clock line |
| 3V3 | OLED VCC | Red | 3.3V power for display |
| GND | OLED GND | Black | Display ground |
| GPIO34 (ADC1_CH6) | Pot1 SIG | Yellow | Voltage simulation ADC input |
| 3V3 | Pot1 VCC | Red | Potentiometer supply |
| GND | Pot1 GND | Black | Potentiometer ground |
| GPIO35 (ADC1_CH7) | Pot2 SIG | Orange | Current simulation ADC input |
| 3V3 | Pot2 VCC | Red | Potentiometer supply |
| GND | Pot2 GND | Black | Potentiometer ground |
| GPIO26 | R1 pin 2 | Lime green | Green LED drive (through 220Ω) |
| R1 pin 1 | Green LED Anode | Lime green | 220Ω resistor to LED |
| Green LED Cathode | GND | Black | LED return path |
| GPIO27 | R2 pin 2 | Tomato red | Red LED drive (through 220Ω) |
| R2 pin 1 | Red LED Anode | Tomato red | 220Ω resistor to LED |
| Red LED Cathode | GND | Black | LED return path |
| GPIO25 | Buzzer pin 1 (+) | Purple | Buzzer drive signal |
| Buzzer pin 2 (−) | GND | Black | Buzzer return path |

---

## 4. Component Details

### ESP32 WROOM-32 DevKit V1
- **Role:** Central microcontroller. Reads two ADC channels, computes power, drives the I2C OLED display, and controls three digital outputs (LED green, LED red, buzzer).
- **Why ESP32:** Dual-core 240MHz, 12-bit ADC, built-in WiFi (needed for Part B), I2C hardware support, 3.3V logic — exactly matched to the SSD1306 display and ACS712 sensor operating range. The DevKit V1 form factor is standard in Wokwi simulations.
- **ADC note:** GPIO34 and GPIO35 are input-only pins on the ESP32 with no internal pull-up/down resistors, making them ideal dedicated ADC channels. ADC1 pins (GPIO32–GPIO39) are preferred because ADC2 is disabled when WiFi is active (relevant in Part B).

### OLED SSD1306 128×64 I2C
- **Role:** Displays title, real-time V/I/P metrics, large power reading, status text, and a horizontal bar graph.
- **Why SSD1306:** Industry-standard low-power OLED with excellent Wokwi support. I2C interface uses only 2 pins (SDA + SCL), leaving GPIO pins free for sensors and actuators. 128×64 resolution provides enough space for the multi-row display layout at textSize 1 and 2. Default I2C address 0x3C.
- **Connection:** SDA → GPIO21, SCL → GPIO22 (ESP32 default I2C pins).

### Potentiometer 1 — Voltage Simulation (GPIO34)
- **Role:** Simulates a resistive voltage divider circuit that would normally scale mains voltage (0–240V) to a safe 0–3.3V ADC input range.
- **Why potentiometer:** Wokwi cannot simulate the non-linear characteristics of a real voltage divider under load, but a potentiometer accurately reproduces the 0–3.3V linear output range that the ADC would receive. Turning the knob simulates different mains voltage levels.
- **Scaling:** `voltage = (analogRead(34) / 4095.0) × 240.0`

### Potentiometer 2 — Current Simulation (GPIO35)
- **Role:** Simulates an ACS712-10A Hall-effect current sensor that outputs 0–3.3V proportional to 0–10A.
- **Why potentiometer:** Same reasoning as Pot1. The ACS712 output is a linear voltage proportional to current. A potentiometer perfectly replicates this behaviour for simulation purposes.
- **Scaling:** `current = (analogRead(35) / 4095.0) × 10.0`

### Green LED + 220Ω Resistor (GPIO26)
- **Role:** Illuminates continuously when power ≤ 150W (SAFE state). Provides immediate visual feedback at a glance.
- **Why 220Ω:** ESP32 GPIO outputs 3.3V. A typical green LED has a forward voltage of ~2.1V and max current of 20mA. R = (3.3 − 2.1) / 0.02 = 60Ω minimum. 220Ω gives ~5.5mA — safe, visible, and within ESP32 GPIO current limits (max 40mA per pin).

### Red LED + 220Ω Resistor (GPIO27)
- **Role:** Illuminates continuously when power > 150W (OVERLOAD state). Used alongside the buzzer for a multi-sensory alert.
- **Why 220Ω:** Same calculation as green LED. Red LED forward voltage ~1.8V: R = (3.3 − 1.8) / 0.02 = 75Ω minimum. 220Ω gives ~6.8mA — safe and visible.

### Active Buzzer 5V (GPIO25)
- **Role:** Sounds a continuous tone during OVERLOAD. Provides an audible alert independent of the display — critical if the device is out of sight.
- **Why active buzzer:** An active buzzer has a built-in oscillator and sounds when DC voltage is applied — no PWM or frequency programming required. Simply driving GPIO25 HIGH triggers the tone. 5V rated buzzers operate adequately at 3.3V in Wokwi simulation (volume is reduced in practice but audible).

---

## 5. Circuit Design Decisions

| Decision | Rationale |
|---|---|
| ADC1 pins only (GPIO34, GPIO35) | ADC2 is disabled when WiFi is active (Part B). Using ADC1 ensures the same pins work across both sketches. |
| I2C for OLED (not SPI) | I2C requires only 2 wires vs SPI's 4, saving GPIO pins for sensors and actuators. |
| 220Ω LED resistors | Conservative current limit — protects both ESP32 GPIO output stage and LEDs during extended operation. |
| Active buzzer (not passive) | Simpler firmware — no tone frequency programming. A single `digitalWrite(HIGH)` sounds the alert. |
| Potentiometers at 3V3 supply | Ensures the wiper voltage range exactly matches the ESP32 ADC input range (0–3.3V), preventing ADC over-voltage. |
| POWER_THRESHOLD = 150W | Assignment specification. Represents a reasonable single-appliance load limit for residential monitoring in Uganda. |

---

## 6. Wiring Diagram (Text)

```
ESP32                    SSD1306 OLED
─────                    ────────────
3V3  ──────────────────► VCC
GND  ──────────────────► GND
D21  ──────────────────► SDA
D22  ──────────────────► SCL

ESP32                    Potentiometer 1 (Voltage)
─────                    ────────────────────────
3V3  ──────────────────► VCC
GND  ──────────────────► GND
D34  ──────────────────► SIG (wiper)

ESP32                    Potentiometer 2 (Current)
─────                    ────────────────────────
3V3  ──────────────────► VCC
GND  ──────────────────► GND
D35  ──────────────────► SIG (wiper)

ESP32    Resistor 220Ω   Green LED
─────    ─────────────   ─────────
D26  ───► R1(2)──R1(1) ──► Anode
                           Cathode ──► GND

ESP32    Resistor 220Ω   Red LED
─────    ─────────────   ───────
D27  ───► R2(2)──R2(1) ──► Anode
                           Cathode ──► GND

ESP32                    Active Buzzer
─────                    ─────────────
D25  ──────────────────► + (pin 1)
GND  ──────────────────── − (pin 2)
```
