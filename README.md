# LoRa → WiFi Gateway using Arduino UNO and M5StickC Plus

## Overview

This project implements a **hybrid IoT gateway** that bridges **LoRa communication to WiFi over MQTT**.

The system uses:

* **Arduino UNO + LoRa Shield** → Handles long-range LoRa communication.
* **M5StickC Plus (ESP32)** → Acts as a WiFi gateway that publishes messages to the internet using **MQTT (Ubidots)**.
* **UART serial communication** between the Arduino and the ESP32.

This architecture allows the system to:

* Use **WiFi when available**
* Fall back to **LoRa communication when WiFi is unavailable**

---

# System Architecture

```
LoRa Network
      │
      ▼
Arduino UNO + LoRa Shield
      │
UART Serial (SoftwareSerial)
      │
      ▼
M5StickC Plus (ESP32)
      │
WiFi
      │
MQTT
      │
      ▼
Ubidots Cloud
```

---

# Hardware Components

* Arduino UNO
* LoRa Shield
* M5StickC Plus (ESP32)
* 2 × 470Ω resistors (voltage divider)
* Jumper wires
* USB cables

---

# Pin Connections

## Arduino UNO ↔ M5StickC Plus

| Device        | Pin    | Function      |
| ------------- | ------ | ------------- |
| Arduino UNO   | Pin 9  | TX (Transmit) |
| Arduino UNO   | Pin 8  | RX (Receive)  |
| M5StickC Plus | GPIO32 | RX            |
| M5StickC Plus | GPIO33 | TX            |
| Arduino UNO   | GND    | Ground        |
| M5StickC Plus | GND    | Ground        |

### Serial Wiring Rule

Serial communication must be **crossed**:

```
Arduino TX → ESP32 RX
Arduino RX ← ESP32 TX
```

### Actual Wiring

```
Arduino Pin 9 (TX) → Voltage Divider → GPIO32 (ESP32 RX)
Arduino Pin 8 (RX) ← GPIO33 (ESP32 TX)
GND ↔ GND
```

---

# Voltage Level Protection

The Arduino UNO operates at **5V logic**, while the ESP32 operates at **3.3V logic**.

Directly connecting the Arduino TX pin to the ESP32 RX pin may damage the ESP32.

To prevent this, a **voltage divider** is used.

Example using two 470Ω resistors:

```
Arduino TX
     |
   470Ω
     |
     +-----> GPIO32 (ESP32 RX)
     |
   470Ω
     |
    GND
```

This reduces the signal from **5V → ~2.5V**, which is still recognized as HIGH by the ESP32.

---

# Why SoftwareSerial Is Used

The Arduino UNO has **only one hardware serial port**.

```
Pin 0 → RX
Pin 1 → TX
```

These pins are already used for:

* USB communication
* Serial Monitor debugging
* Uploading sketches

If we used pins **0 and 1** for the ESP32 connection:

* Code uploads would fail
* Serial Monitor debugging would break
* Conflicts would occur during communication

To solve this problem, we use **SoftwareSerial**, which allows serial communication on other pins.

Example used in this project:

```
Pin 8 → RX
Pin 9 → TX
```

This allows:

* Arduino ↔ ESP32 communication
* Serial Monitor debugging to continue working

---

# Arduino Code Concept

The Arduino performs two tasks:

1. Receive LoRa messages
2. Forward messages to the ESP32 via UART

Example concept:

```cpp
#include <SoftwareSerial.h>

SoftwareSerial espSerial(8, 9);

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
}

void loop() {
  if (espSerial.available()) {
    String msg = espSerial.readStringUntil('\n');
    Serial.println(msg);
  }
}
```

---

# M5StickC Plus Role

The M5StickC Plus acts as the **Internet gateway**.

It performs the following functions:

1. Connect to WiFi
2. Connect to MQTT broker (Ubidots)
3. Receive UART data from Arduino
4. Convert messages to JSON
5. Publish messages to MQTT

---

# MQTT Message Format

Messages are sent to Ubidots in JSON format.

Example:

```
{
  "heartbeat": 1,
  "raw": "LoRa message received"
}
```

MQTT topic used (All to use the same TOKEN):



```
Shafiq /v1.6/devices/m5stackcplus_gateway/heartbeat_2
Natalie /v1.6/devices/m5stackcplus_gateway/heartbeat_3
Yan Hyee /v1.6/devices/m5stackcplus_gateway/heartbeat_4
Koel /v1.6/devices/m5stackcplus_gateway/heartbeat_5

Natalie /v1.6/devices/m5stackcplus_gateway/smokesensor_3
Yan Hyee /v1.6/devices/m5stackcplus_gateway/smokesensor_4
```

---

# Step-by-Step Setup Guide

## 1 Install Arduino IDE Support

Add M5Stack board manager URL:

```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

Then install:

```
Boards Manager → M5Stack
```

---

## 2 Install Required Libraries

From **Library Manager** install:

* PubSubClient
* ArduinoJson
* M5StickCPlus

WiFi library is already included with ESP32.

---

## 3 Upload ESP32 Code

1. Connect M5StickC Plus via USB
2. Select board:

```
Tools → Board → M5Stick-CPlus
```

3. Upload the gateway code.

---

## 4 Upload Arduino Code

1. Select board:

```
Tools → Board → Arduino UNO
```

2. Upload the LoRa communication sketch.

---

## 5 Perform Wiring

After uploading code to both devices:

Connect UART wires:

```
Arduino TX → ESP32 RX
Arduino RX ← ESP32 TX
GND ↔ GND
```

---

## 6 Power Both Devices

Power both devices using USB.

The M5StickC Plus will:

* connect to WiFi
* connect to MQTT
* display status on the LCD screen.

---

# Display Information

The M5StickC Plus screen shows:

```
WiFi : OK / FAIL
MQTT : OK / --
Last message received
```

This helps with debugging.

---

# Fallback Logic

If WiFi fails:

1. ESP32 detects WiFi disconnection
2. ESP32 sends `"WIFI_DOWN"` to Arduino
3. Arduino continues LoRa communication

This ensures **network redundancy**.

---

# Key Technologies Used

* LoRa
* ESP32 WiFi
* MQTT protocol
* UART Serial Communication
* JSON message formatting
* Ubidots IoT Cloud

---

# Key Concepts

| Concept         | Purpose                                 |
| --------------- | --------------------------------------- |
| SoftwareSerial  | Avoids using Arduino pins 0 and 1       |
| UART            | Communication between Arduino and ESP32 |
| WiFi            | Internet connectivity                   |
| MQTT            | Lightweight IoT messaging protocol      |
| JSON            | Structured data format                  |
| Voltage Divider | Protect ESP32 from 5V signals           |

---

# Summary (Simple Explanation)

The system works like this:

1. LoRa messages arrive at the Arduino.
2. Arduino sends the message to the M5StickC Plus.
3. The M5StickC Plus connects to WiFi.
4. The message is published to the cloud using MQTT.

If WiFi stops working, the system continues using **LoRa communication**.

This creates a **reliable IoT gateway with wireless redundancy**.

---
