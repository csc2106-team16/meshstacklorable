# LoRa Mesh + WiFi/MQTT + BLE — Air Quality Monitoring Gateway
 
## Overview
 
This project implements a **multi-protocol IoT air quality monitoring system** with automatic network failover. Each node combines three wireless technologies to ensure reliable smoke detection data delivery.
 
The system uses:
 
* **Arduino UNO + LoRa Shield (Cytron SHIELD-LORA-RFM)** → Handles long-range LoRa mesh communication with flooding-based relay.
* **M5StickC Plus (ESP32)** → Acts as a WiFi/MQTT gateway, BLE broadcaster, and mode controller.
* **MQ2 Smoke Sensor** → Detects smoke/gas levels on sender nodes.
* **UART serial communication** between the Arduino and the ESP32.
* **TLS-encrypted MQTT** to Ubidots IoT Cloud.
 
The architecture provides **triple-layer redundancy**:
 
1. **WiFi/MQTT** — Primary path. Sensor data is published to Ubidots cloud over TLS.
2. **LoRa Mesh** — Automatic fallback when WiFi drops. Flooding mesh with multi-hop relay.
3. **BLE Advertising** — Always-on local broadcast for nearby listeners.
 
---
 
## System Architecture
 
```
                     ┌────────────────────────┐
                     │     Ubidots Cloud       │
                     │   (MQTT over TLS)       │
                     └──────────▲─────────────┘
                                │
                         WiFi / MQTT
                                │
┌───────────────────────────────┼───────────────────────────────┐
│  NODE 1 (Sender)              │         NODE 2 (Receiver)     │
│                               │                               │
│  ┌─────────────┐    UART    ┌─┴───────────┐    UART    ┌─────────────┐
│  │ Arduino UNO │◄──────────►│ M5StickC    │    │       │ M5StickC    │◄──────────►│ Arduino UNO │
│  │ + LoRa      │            │ Plus        │    │       │ Plus        │            │ + LoRa      │
│  │ + MQ2 Sensor│            │ (Gateway)   │    │       │ (Gateway)   │            │ (Relay only)│
│  └──────┬──────┘            └──────┬──────┘    │       └──────┬──────┘            └──────┬──────┘
│         │                          │           │              │                          │
│         │    LoRa 915.5 MHz        │           │              │                          │
│         ◄──────────────────────────┼───────────┼──────────────┼──────────────────────────►
│                                    │           │              │
│                               BLE Advert  BLE Advert
│                                    │           │              │
└───────────────────────────────────────────────────────────────┘
```
 
---
 
## Hardware Components
 
* Arduino UNO (×2)
* Cytron SHIELD-LORA-RFM N1AQ4 LoRa Shield (×2)
* M5StickC Plus ESP32 (×2)
* MQ2 Smoke/Gas Sensor (on sender node)
* Jumper wires
* USB cables for power
 
---
 
## File Structure
 
| File | Device | Role |
| --- | --- | --- |
| `p2p_node_sender_mesh.ino` | Arduino UNO (Node 1) | Reads MQ2 sensor, originates LoRa mesh packets, forwards data to M5Stick via UART |
| `p2p_node_receiver_mesh.ino` | Arduino UNO (Node 2) | Receives and relays LoRa mesh packets, forwards data to M5Stick via UART |
| `m5stick_integrated_sender.ino` | M5StickC Plus (Node 1) | WiFi/MQTT publisher, BLE broadcaster, LoRa mode controller for sender |
| `m5stick_receiver.ino` | M5StickC Plus (Node 2) | WiFi/MQTT subscriber, BLE broadcaster, LoRa mode controller for receiver |
| `gateway.h` / `gateway.cpp` | M5StickC Plus (shared) | TLS MQTT client library — WiFi connection, MQTT publish, Ubidots integration |
 
---
 
## Pin Connections
 
### Arduino UNO ↔ M5StickC Plus (UART)
 
| Device | Pin | Function |
| --- | --- | --- |
| Arduino UNO | Pin 4 | TX (SoftwareSerial) |
| Arduino UNO | Pin 5 | RX (SoftwareSerial) |
| M5StickC Plus | GPIO 32 (Grove RX) | RX |
| M5StickC Plus | GPIO 33 (Grove TX) | TX |
| Both | GND | Common ground |
 
Serial communication must be **crossed**:
 
```
Arduino Pin 4 (TX) → M5StickC Plus GPIO 32 (RX)
Arduino Pin 5 (RX) ← M5StickC Plus GPIO 33 (TX)
GND ↔ GND
```
 
### Arduino UNO — LoRa Shield Pins
 
| Pin | Function |
| --- | --- |
| 10 | RFM95 Chip Select (CS) |
| 9 | RFM95 Reset (RST) |
| 2 | RFM95 Interrupt (INT) |
| A0 | MQ2 Smoke Sensor analog input (sender only) |
 
---
 
## Communication Protocols
 
### UART Command Protocol (M5Stick ↔ Arduino)
 
**M5Stick → Arduino (commands):**
 
| Command | Effect |
| --- | --- |
| `LORA,ON\n` | Enable LoRa TX/RX on Arduino |
| `LORA,OFF\n` | Disable LoRa TX/RX on Arduino |
 
**Arduino → M5Stick (data):**
 
| Message | Meaning |
| --- | --- |
| `READY,<id>\n` | Arduino initialized, reports node ID |
| `N<id>,R<raw>,V<voltage>,<ALERT\|CLEAR>\n` | Sensor reading or relayed LoRa data |
 
Example data message: `N1,R245,V1.20,ALERT`
 
### LoRa Mesh Protocol
 
The system uses a **flooding mesh** topology. Every node that receives a packet rebroadcasts it (with decremented TTL) so messages can reach nodes beyond direct radio range.
 
**Mesh packet structure:**
 
| Field | Type | Description |
| --- | --- | --- |
| `originId` | uint8 | Node that originated the message |
| `lastHopId` | uint8 | Node that last relayed the message |
| `msgId` | uint16 | Sequence number from origin node |
| `ttl` | uint8 | Time-to-live (default 3 hops) |
| `payload` | char[40] | XOR-encrypted sensor data |
| `checksum` | uint8 | XOR checksum for integrity |
 
**Mesh features:**
 
* **Deduplication cache** — 16-entry ring buffer prevents rebroadcasting the same packet.
* **Random relay delay** — 50–200 ms jitter reduces collision probability.
* **XOR cipher** — Lightweight payload encryption using key `0x5A`.
* **Checksum validation** — Drops corrupted packets.
 
**Radio settings:** 915.5 MHz, TX power 23 dBm.
 
### MQTT (Ubidots Cloud)
 
Messages are published over **TLS (port 8883)** to `industrial.api.ubidots.com`.
 
**Topics per node:**
 
| Node | Heartbeat Topic | Smoke Sensor Topic |
| --- | --- | --- |
| Node 1 (Shafiq) | `/v1.6/devices/m5stick_node1/heartbeat_1` | `/v1.6/devices/m5stick_node1/smokesensor_4` |
 
Heartbeat payload format:
 
```json
{
  "heartbeat_1": 1,
  "raw": "N1,R245,V1.20,ALERT"
}
```
 
Smoke sensor payload format:
 
```json
{
  "smokesensor_4": 245
}
```
 
**Additional team member topics (shared Ubidots token):**
 
| Person | Heartbeat | Smoke Sensor |
| --- | --- | --- |
| Shafiq | `heartbeat_2` | — |
| Natalie | `heartbeat_3` | `smokesensor_3` |
| Yan Hyee | `heartbeat_4` | `smokesensor_4` |
| Koel | `heartbeat_5` | — |
 
### BLE Advertising
 
Each M5StickC Plus continuously broadcasts sensor data via BLE manufacturer-specific advertising. This provides a local monitoring channel that works independently of WiFi and LoRa.
 
**BLE device names:** `ENV-NODE-01`, `ENV-NODE-02`
 
**Payload format:** `SMK|<nodeId>|<rawValue>|<voltage>|<ALERT|CLEAR>`
 
Example: `SMK|1|245|1.20|ALERT`
 
---
 
## WiFi/LoRa Automatic Failover
 
The M5StickC Plus manages network mode switching automatically:
 
```
                    ┌──────────┐
       Power on ───►│ WiFi Mode│ (primary)
                    └────┬─────┘
                         │
              WiFi down for 5s?
                         │ YES
                         ▼
                    ┌──────────┐
                    │LoRa Mode │ (fallback)
                    └────┬─────┘
                         │
                   WiFi restored?
                         │ YES
                         ▼
                    ┌──────────┐
                    │ WiFi Mode│
                    └──────────┘
```
 
**Detailed behavior:**
 
1. On startup, WiFi is primary. M5Stick sends `LORA,OFF` to Arduino.
2. WiFi health is checked every 5 seconds.
3. If WiFi drops and stays down for 5 seconds, M5Stick switches to LoRa mode and sends `LORA,ON` to Arduino.
4. Arduino begins LoRa TX (sender) or RX/relay (receiver).
5. When WiFi reconnects, M5Stick sends `LORA,OFF` and resumes MQTT publishing.
6. **Sensor data is always sent to M5Stick via UART**, regardless of mode.
7. **BLE advertising is always active**, regardless of mode.
 
---
 
## Security
 
* **LoRa payload encryption** — XOR cipher with shared key `0x5A`. Lightweight protection suitable for prototype/educational use.
* **MQTT over TLS** — Connection to Ubidots uses port 8883 with TLS encryption. Certificate verification is relaxed (prototype mode via `setInsecure()`).
* **Checksum integrity** — Each LoRa mesh packet includes an XOR checksum; corrupted packets are dropped.
 
---
 
## Display Information
 
The M5StickC Plus LCD shows real-time status:
 
**WiFi Mode:**
 
```
WIFI/MQTT [TX]              BLE:ON
MY:1                      FROM:2
       !! ALERT !!
R:245  V:1.20V
WiFi:OK  MQTT:OK
```
 
**LoRa Fallback Mode:**
 
```
!! LORA FALLBACK !!      WiFi:DOWN
MY:1                      FROM:2
       !! ALERT !!
R:245  V:1.20V
BLE:ON  LoRa:ACTIVE
```
 
Screen background changes to **red** on ALERT and **green** on CLEAR.
 
---
 
## MQ2 Smoke Sensor Configuration
 
| Parameter | Value |
| --- | --- |
| Analog pin | A0 |
| Warmup time | 30 seconds |
| Smoke threshold | Raw ADC ≥ 100 |
| Sample interval | 1 second |
| Alert repeat interval | 5 seconds |
| All-clear repeat interval | 10 seconds |
 
---
 
## Setup Guide
 
### 1. Install Arduino IDE Board Support
 
Add the M5Stack board manager URL in Arduino IDE preferences:
 
```
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```
 
Then install **M5Stack** from Boards Manager.
 
### 2. Install Required Libraries
 
From Library Manager, install:
 
* **M5StickCPlus** — Display and hardware control
* **PubSubClient** — MQTT client
* **NimBLE-Arduino** — BLE advertising
* **RadioHead** — LoRa radio driver (RH_RF95)
 
WiFi and SPI libraries are included with ESP32 and Arduino cores.
 
### 3. Configure Node Identity
 
Before uploading, edit the `#define` values in each sketch:
 
**Arduino sketches** — set `MY_NODE_ID` (must be unique per node).
 
**M5Stick sketches** — set `MY_NODE_ID`, `BLE_DEVICE_NAME`, `WIFI_SSID`, and `WIFI_PASSWORD`.
 
**Gateway library** — set `DEVICE_LABEL` and `HEARTBEAT_KEY` in `gateway.cpp` to match your Ubidots device.
 
### 4. Upload Code
 
**For M5StickC Plus:**
 
1. Connect via USB.
2. Select board: `Tools → Board → M5Stick-CPlus`
3. Place `gateway.h` and `gateway.cpp` in the same folder as the `.ino` file.
4. Upload.
 
**For Arduino UNO:**
 
1. Connect via USB.
2. Select board: `Tools → Board → Arduino UNO`
3. Upload the appropriate sender or receiver sketch.
 
### 5. Wire UART Connections
 
After uploading to both devices, connect UART wires between each Arduino–M5Stick pair:
 
```
Arduino Pin 4 (TX) → M5StickC Plus GPIO 32 (RX)
Arduino Pin 5 (RX) ← M5StickC Plus GPIO 33 (TX)
GND ↔ GND
```
 
### 6. Power and Verify
 
Power both devices via USB. The M5StickC Plus will display WiFi/MQTT connection status on its LCD screen. The Arduino Serial Monitor (9600 baud) shows LoRa and sensor activity.
 
---
 
## Key Technologies
 
| Technology | Purpose |
| --- | --- |
| LoRa 915.5 MHz | Long-range fallback communication |
| Flooding Mesh | Multi-hop packet relay without routing tables |
| ESP32 WiFi | Primary internet connectivity |
| MQTT over TLS | Secure IoT cloud messaging (Ubidots) |
| BLE Advertising | Always-on local data broadcast |
| UART (SoftwareSerial) | Arduino ↔ M5Stick bidirectional link |
| XOR Cipher | Lightweight LoRa payload encryption |
| JSON | Structured MQTT message format |
 
---
 
## Summary
 
The system creates a **resilient IoT smoke detection network** with three independent communication layers:
 
1. **WiFi/MQTT** publishes sensor data to the cloud in real time.
2. **LoRa mesh** kicks in automatically when WiFi fails, relaying alerts across multiple hops.
3. **BLE** broadcasts locally at all times for nearby monitoring devices.
 
Each node pair (Arduino + M5StickC Plus) operates autonomously, with the M5Stick managing mode switching and the Arduino handling sensor reading and LoRa radio operations. This architecture ensures that smoke alerts are delivered reliably even when internet connectivity is lost.