/*
 * LoRa FLOODING MESH Node — Receiver + Relay
 * Hardware: Arduino Uno + Cytron SHIELD-LORA-RFM N1AQ4
 *
 * INTEGRATED WITH M5STICK WIFI/LORA SWITCHING:
 *   - LoRa RX/relay is OFF by default (WiFi is primary)
 *   - M5Stick sends "LORA,ON" when WiFi drops → Arduino starts LoRa RX/relay
 *   - M5Stick sends "LORA,OFF" when WiFi returns → Arduino stops LoRa RX/relay
 *   - Forwards received LoRa data to M5Stick via UART when enabled
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
#define MY_NODE_ID   2

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

#define SOFT_TX_PIN  4   // Arduino TX → M5StickC Plus RX (Grove Pin 32)
#define SOFT_RX_PIN  5   // Arduino RX ← M5StickC Plus TX (Grove Pin 33)

// ---------------------------------------------------------------
// RADIO SETTINGS
// ---------------------------------------------------------------
#define RF95_FREQ   915.5
#define TX_POWER    23

// ---------------------------------------------------------------
// MESH PACKET STRUCTURE — must match sender nodes exactly
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

// LoRa enable/disable — controlled by M5Stick commands
bool loraEnabled = false;   // OFF by default (WiFi is primary)

String m5CmdBuffer = "";    // Buffer for incoming commands from M5Stick

// ---------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  m5Serial.begin(9600);

  randomSeed(analogRead(A0));
  memset(dedupCache, 0, sizeof(dedupCache));

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
  Serial.print(F("  LoRa MESH Receiver Node Ready. ID: "));
  Serial.println(MY_NODE_ID);
  Serial.println(F("  LoRa RX: OFF (waiting for M5Stick)"));
  Serial.print(F("  Dedup cache: ")); Serial.print(DEDUP_CACHE_SIZE);
  Serial.println(F(" entries"));
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

  // Forward to M5Stick via UART
  m5Serial.println(decrypted);

  // Parse for serial monitor display
  char payloadCopy[40];
  strncpy(payloadCopy, decrypted.c_str(), sizeof(payloadCopy));
  payloadCopy[sizeof(payloadCopy) - 1] = '\0';

  char* nodeToken   = strtok(payloadCopy, ",");
  char* rawToken    = strtok(NULL, ",");
  char* voltToken   = strtok(NULL, ",");
  char* statusToken = strtok(NULL, ",");

  int   nodeId  = nodeToken  ? atoi(nodeToken  + 1) : incoming->originId;
  int   rawVal  = rawToken   ? atoi(rawToken   + 1) : -1;
  float voltage = voltToken  ? atof(voltToken  + 1) : -1.0;
  bool  isAlert = statusToken && strcmp(statusToken, "ALERT") == 0;
  bool  isClear = statusToken && strcmp(statusToken, "CLEAR") == 0;

  Serial.println(F("--------------------------------------------"));
  Serial.println(F("  [MESH PACKET RECEIVED]"));
  Serial.print(F("  Origin node : ")); Serial.println(incoming->originId);
  Serial.print(F("  Last hop    : Node ")); Serial.println(incoming->lastHopId);
  Serial.print(F("  MsgID       : ")); Serial.println(incoming->msgId);
  Serial.print(F("  TTL left    : ")); Serial.println(incoming->ttl);
  Serial.print(F("  Hops taken  : ")); Serial.println(MESH_TTL_DEFAULT - incoming->ttl);
  Serial.print(F("  RSSI (dBm)  : ")); Serial.println(rf95.lastRssi());
  Serial.print(F("  Raw ADC     : ")); Serial.print(rawVal);
                                         Serial.println(F(" / 1023"));
  Serial.print(F("  Voltage     : ")); Serial.print(voltage, 2);
                                         Serial.println(F(" V"));
  Serial.print(F("  Smoke Status: "));
  if (isAlert) {
    Serial.println(F("*** SMOKE ALERT ***"));
  } else if (isClear) {
    Serial.println(F("ALL CLEAR"));
  } else {
    Serial.println(F("Unknown status."));
  }
  Serial.println(F("--------------------------------------------"));

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