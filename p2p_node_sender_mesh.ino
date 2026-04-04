/*
 * LoRa FLOODING MESH Node — Smoke Sensor (Sender + Relay)
 * Hardware: Arduino Uno + Cytron SHIELD-LORA-RFM N1AQ4 + MQ2 Smoke Sensor
 *
 * INTEGRATED WITH M5STICK WIFI/LORA SWITCHING:
 *   - LoRa TX is OFF by default (WiFi is primary)
 *   - M5Stick sends "LORA,ON" when WiFi drops → Arduino starts LoRa TX
 *   - M5Stick sends "LORA,OFF" when WiFi returns → Arduino stops LoRa TX
 *   - Sensor data is ALWAYS sent to M5Stick via UART regardless of mode
 *   - LoRa RX + relay only active when loraEnabled = true
 *
 * UART Wiring (Bidirectional):
 *   Arduino Pin 4 (TX) → M5StickC Plus Grove Port PIN 32 (RX)
 *   Arduino Pin 5 (RX) ← M5StickC Plus Grove Port PIN 33 (TX)
 *   Arduino GND        ← M5StickC Plus GND
 *
 * Change MY_NODE_ID for each physical node before uploading.
 */

#include <SPI.h>
#include <RH_RF95.h>
#include <SoftwareSerial.h>

// ---------------------------------------------------------------
// NODE IDENTITY — change this for each node (must be unique)
// ---------------------------------------------------------------
#define MY_NODE_ID   1

// ---------------------------------------------------------------
// FLOODING MESH SETTINGS
// ---------------------------------------------------------------
#define MESH_TTL_DEFAULT     3
#define DEDUP_CACHE_SIZE    16
#define RELAY_DELAY_MIN_MS  50
#define RELAY_DELAY_MAX_MS  200

// ---------------------------------------------------------------
// [SECURITY] --- LoRa lightweight security setup ---
// ---------------------------------------------------------------
const uint8_t LORA_KEY = 0x5A;
uint16_t packetCounter = 0;

String xorCipher(const String& input) {
  String out = input;
  for (size_t i = 0; i < out.length(); i++) {
    out[i] = out[i] ^ LORA_KEY;
  }
  return out;
}

// ---------------------------------------------------------------
// PIN DEFINITIONS
// ---------------------------------------------------------------
#define RFM95_CS    10
#define RFM95_RST    9
#define RFM95_INT    2
#define MQ2_PIN     A0

#define SOFT_TX_PIN  4   // Arduino TX → M5StickC Plus RX (Grove Pin 32)
#define SOFT_RX_PIN  5   // Arduino RX ← M5StickC Plus TX (Grove Pin 33)

// ---------------------------------------------------------------
// RADIO & SENSOR SETTINGS
// ---------------------------------------------------------------
#define RF95_FREQ           915.5
#define TX_POWER            23
#define SMOKE_THRESHOLD     100
#define MQ2_WARMUP_MS       30000
#define SAMPLE_INTERVAL_MS  1000
#define ALERT_REPEAT_MS     5000
#define CLEAR_REPEAT_MS     10000

// ---------------------------------------------------------------
// MESH PACKET STRUCTURE
// ---------------------------------------------------------------
struct MeshMessage {
  uint8_t  originId;
  uint8_t  lastHopId;
  uint16_t msgId;
  uint8_t  ttl;
  char     payload[40];
  uint8_t  checksum;
};

// Forward declarations
uint8_t calculateChecksum(MeshMessage *msg);
void handleIncoming();
void originateLoRa(int rawValue, bool isAlert);
void sendToM5Stick(int rawValue, bool isAlert);
void checkM5StickCommands();

// ---------------------------------------------------------------
// DEDUP CACHE
// ---------------------------------------------------------------
struct SeenEntry {
  uint8_t  originId;
  uint16_t msgId;
};

SeenEntry dedupCache[DEDUP_CACHE_SIZE];
uint8_t   dedupIndex = 0;

bool alreadySeen(uint8_t originId, uint16_t msgId) {
  for (uint8_t i = 0; i < DEDUP_CACHE_SIZE; i++) {
    if (dedupCache[i].originId == originId && dedupCache[i].msgId == msgId) {
      return true;
    }
  }
  return false;
}

void recordSeen(uint8_t originId, uint16_t msgId) {
  dedupCache[dedupIndex].originId = originId;
  dedupCache[dedupIndex].msgId    = msgId;
  dedupIndex = (dedupIndex + 1) % DEDUP_CACHE_SIZE;
}

// ---------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------
RH_RF95        rf95(RFM95_CS, RFM95_INT);
SoftwareSerial m5Serial(SOFT_RX_PIN, SOFT_TX_PIN);

unsigned long lastSampleTime   = 0;
unsigned long lastTransmitTime = 0;
uint16_t      localMsgCounter  = 0;

bool smokeActive  = false;
bool allClearSent = true;

// LoRa enable/disable — controlled by M5Stick commands
bool loraEnabled = false;   // OFF by default (WiFi is primary)

String m5CmdBuffer = "";    // Buffer for incoming commands from M5Stick

// ---------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  m5Serial.begin(9600);

  randomSeed(analogRead(A1));
  memset(dedupCache, 0, sizeof(dedupCache));

  Serial.println(F("MQ2 warming up (30s)..."));
  delay(MQ2_WARMUP_MS);
  Serial.println(F("MQ2 ready."));

  // LoRa hardware reset
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH); delay(10);
  digitalWrite(RFM95_RST, LOW);  delay(10);
  digitalWrite(RFM95_RST, HIGH); delay(10);

  if (!rf95.init()) {
    Serial.println(F("LoRa init failed!"));
    while (1);
  }

  rf95.setFrequency(RF95_FREQ);
  rf95.setTxPower(TX_POWER, false);

  Serial.println(F("============================================"));
  Serial.print(F("  LoRa MESH Sender Node Ready. ID: "));
  Serial.println(MY_NODE_ID);
  Serial.println(F("  LoRa TX: OFF (waiting for M5Stick)"));
  Serial.println(F("  UART sensor data: always ON"));
  Serial.println(F("============================================"));

  m5Serial.print(F("READY,"));
  m5Serial.println(MY_NODE_ID);
}

// ---------------------------------------------------------------
void loop() {
  // --- 1. Check for commands from M5Stick ---
  checkM5StickCommands();

  // --- 2. Handle incoming LoRa packets (only if enabled) ---
  if (loraEnabled && rf95.available()) {
    handleIncoming();
  }

  // --- 3. Sample own sensor ---
  if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = millis();

    int  rawValue = analogRead(MQ2_PIN);
    bool smokeNow = (rawValue >= SMOKE_THRESHOLD);

    // ALWAYS send sensor data to M5Stick via UART (regardless of mode)
    sendToM5Stick(rawValue, smokeNow);

    // LoRa TX only if enabled by M5Stick
    if (loraEnabled) {
      if (smokeNow && !smokeActive) {
        smokeActive  = true;
        allClearSent = false;
        Serial.println(F("!!! SMOKE DETECTED — sending LoRa alert !!!"));
        originateLoRa(rawValue, true);
        lastTransmitTime = millis();
      }
      else if (smokeNow && smokeActive) {
        if (millis() - lastTransmitTime >= ALERT_REPEAT_MS) {
          Serial.println(F("!!! SMOKE PERSISTS — resending LoRa alert !!!"));
          originateLoRa(rawValue, true);
          lastTransmitTime = millis();
        }
      }
      else if (!smokeNow && smokeActive) {
        smokeActive = false;
        Serial.println(F("Smoke cleared — sending LoRa ALL CLEAR."));
        originateLoRa(rawValue, false);
        lastTransmitTime = millis();
      }
      else if (!smokeNow && !smokeActive && !allClearSent) {
        if (millis() - lastTransmitTime >= CLEAR_REPEAT_MS) {
          Serial.println(F("Resending LoRa ALL CLEAR confirmation."));
          originateLoRa(rawValue, false);
          lastTransmitTime = millis();
          allClearSent = true;
        }
      }
      else {
        Serial.print(F("Clear. Raw: "));
        Serial.print(rawValue);
        Serial.println(F(" (LoRa: no TX needed)"));
      }
    }
    else {
      // LoRa disabled — still track smoke state for when it's re-enabled
      if (smokeNow && !smokeActive) {
        smokeActive  = true;
        allClearSent = false;
      } else if (!smokeNow && smokeActive) {
        smokeActive = false;
      }

      Serial.print(F("Raw: "));
      Serial.print(rawValue);
      Serial.println(F(" (LoRa OFF, UART only)"));
    }
  }
}

// ---------------------------------------------------------------
// Check for LORA,ON / LORA,OFF commands from M5Stick
// ---------------------------------------------------------------
void checkM5StickCommands() {
  while (m5Serial.available()) {
    char c = m5Serial.read();
    if (c == '\n') {
      m5CmdBuffer.trim();
      if (m5CmdBuffer == "LORA,ON") {
        loraEnabled = true;
        Serial.println(F("[CMD] LoRa ENABLED by M5Stick"));
      }
      else if (m5CmdBuffer == "LORA,OFF") {
        loraEnabled = false;
        Serial.println(F("[CMD] LoRa DISABLED by M5Stick"));
      }
      else if (m5CmdBuffer.length() > 0) {
        Serial.print(F("[CMD] Unknown: "));
        Serial.println(m5CmdBuffer);
      }
      m5CmdBuffer = "";
    } else {
      m5CmdBuffer += c;
    }
  }
}

// ---------------------------------------------------------------
// ORIGINATE a new mesh packet (we are the source)
// ---------------------------------------------------------------
void originateLoRa(int rawValue, bool isAlert) {
  float voltage = rawValue * (5.0 / 1023.0);

  MeshMessage msg;
  msg.originId  = MY_NODE_ID;
  msg.lastHopId = MY_NODE_ID;
  msg.msgId     = localMsgCounter++;
  msg.ttl       = MESH_TTL_DEFAULT;

  char voltStr[8];
  dtostrf(voltage, 4, 2, voltStr);

  snprintf(msg.payload, sizeof(msg.payload),
           "N%d,R%d,V%s,%s",
           MY_NODE_ID, rawValue, voltStr,
           isAlert ? "ALERT" : "CLEAR");

  String encrypted = xorCipher(String(msg.payload));
  strncpy(msg.payload, encrypted.c_str(), sizeof(msg.payload) - 1);
  msg.payload[sizeof(msg.payload) - 1] = '\0';

  msg.checksum = calculateChecksum(&msg);
  recordSeen(msg.originId, msg.msgId);

  rf95.send((uint8_t*)&msg, sizeof(msg));
  rf95.waitPacketSent();

  Serial.println(F("--- [TX] Originated mesh packet ---"));
  Serial.print(F("  MsgID : ")); Serial.println(msg.msgId);
  Serial.print(F("  TTL   : ")); Serial.println(msg.ttl);
  Serial.print(F("  Status: ")); Serial.println(isAlert ? "ALERT" : "CLEAR");
}

// ---------------------------------------------------------------
// HANDLE incoming packet — process + relay if appropriate
// ---------------------------------------------------------------
void handleIncoming() {
  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);

  if (!rf95.recv(buf, &len)) return;
  if (len < sizeof(MeshMessage)) return;

  MeshMessage* incoming = (MeshMessage*)buf;

  uint8_t calcCheck = calculateChecksum(incoming);
  if (calcCheck != incoming->checksum) {
    Serial.println(F("[MESH] Checksum mismatch — dropped."));
    return;
  }

  if (alreadySeen(incoming->originId, incoming->msgId)) {
    Serial.print(F("[MESH] Duplicate from origin "));
    Serial.print(incoming->originId);
    Serial.print(F(" msgId "));
    Serial.print(incoming->msgId);
    Serial.println(F(" — dropped."));
    return;
  }

  recordSeen(incoming->originId, incoming->msgId);

  String decrypted = xorCipher(String(incoming->payload));

  Serial.println(F("--- [RX] Mesh packet received ---"));
  Serial.print(F("  Origin   : Node ")); Serial.println(incoming->originId);
  Serial.print(F("  Last hop : Node ")); Serial.println(incoming->lastHopId);
  Serial.print(F("  MsgID    : "));      Serial.println(incoming->msgId);
  Serial.print(F("  TTL left : "));      Serial.println(incoming->ttl);
  Serial.print(F("  RSSI     : "));      Serial.println(rf95.lastRssi());
  Serial.print(F("  Payload  : "));      Serial.println(decrypted);

  // Forward to M5Stick via UART
  m5Serial.println(decrypted);

  // Relay if TTL > 1
  if (incoming->ttl > 1) {
    delay(random(RELAY_DELAY_MIN_MS, RELAY_DELAY_MAX_MS));

    MeshMessage relay;
    memcpy(&relay, incoming, sizeof(MeshMessage));
    relay.lastHopId = MY_NODE_ID;
    relay.ttl       = incoming->ttl - 1;
    relay.checksum  = calculateChecksum(&relay);

    rf95.send((uint8_t*)&relay, sizeof(relay));
    rf95.waitPacketSent();

    Serial.print(F("[MESH] Relayed (TTL now "));
    Serial.print(relay.ttl);
    Serial.println(F(")"));
  } else {
    Serial.println(F("[MESH] TTL expired — not relaying."));
  }
}

// ---------------------------------------------------------------
// Forward own sensor reading to M5StickC Plus via UART
// ---------------------------------------------------------------
void sendToM5Stick(int rawValue, bool isAlert) {
  float voltage = rawValue * (5.0 / 1023.0);

  char voltStr[8];
  dtostrf(voltage, 4, 2, voltStr);

  char uartStr[40];
  snprintf(uartStr, sizeof(uartStr),
           "N%d,R%d,V%s,%s",
           MY_NODE_ID, rawValue, voltStr,
           isAlert ? "ALERT" : "CLEAR");

  m5Serial.println(uartStr);
  delay(10);
  checkM5StickCommands();
}

// ---------------------------------------------------------------
uint8_t calculateChecksum(MeshMessage *msg) {
  uint8_t checksum = 0;
  checksum ^= msg->originId;
  checksum ^= msg->lastHopId;
  checksum ^= (uint8_t)(msg->msgId & 0xFF);
  checksum ^= (uint8_t)(msg->msgId >> 8);
  checksum ^= msg->ttl;
  for (int i = 0; i < (int)strlen(msg->payload); i++) {
    checksum ^= (uint8_t)msg->payload[i];
  }
  return checksum;
}