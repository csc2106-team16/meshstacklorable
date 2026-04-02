/*
 * LoRa P2P Node — Smoke Sensor (Alert-Only Mode, with M5StickC Plus UART output)
 * Hardware: Arduino Uno + Cytron SHIELD-LORA-RFM N1AQ4 + MQ2 Smoke Sensor
 *
 * Transmits ONLY on ALERT or CLEAR transitions via LoRa.
 * Forwards own sensor status to M5StickC Plus via UART at every sample.
 *
 * UART Wiring (Arduino → M5StickC Plus):
 *   Arduino Pin 4 (TX) → M5StickC Plus Grove Port PIN 32 (RX)
 *   Arduino GND        → M5StickC Plus GND
 *
 * Change MY_NODE_ID for each physical sender node before uploading.
 */

#include <SPI.h>
#include <RH_RF95.h>
#include <SoftwareSerial.h>

// [SECURITY ADDED] --- LoRa lightweight security setup ---
const uint8_t LORA_KEY = 0x5A;
uint16_t packetCounter = 0;

String xorCipher(const String& input) {
  String out = input;
  for (size_t i = 0; i < out.length(); i++) {
    out[i] = out[i] ^ LORA_KEY;
  }
  return out;
}

uint8_t simpleChecksum(const String& input) {
  uint8_t sum = 0;
  for (size_t i = 0; i < input.length(); i++) {
    sum ^= (uint8_t)input[i];
  }
  return sum;
}
// ---------------------------------------------------------------
// NODE IDENTITY — change this for each sender node (1 or 2)
// ---------------------------------------------------------------
#define MY_NODE_ID   1

// ---------------------------------------------------------------
// PIN DEFINITIONS
// ---------------------------------------------------------------
#define RFM95_CS    10
#define RFM95_RST    9
#define RFM95_INT    2
#define MQ2_PIN     A0

#define SOFT_TX_PIN  4   // Arduino TX → M5StickC Plus RX (Grove Pin 32)
#define SOFT_RX_PIN  5   // Unused but required by SoftwareSerial

// ---------------------------------------------------------------
// RADIO & SENSOR SETTINGS
// ---------------------------------------------------------------
#define RF95_FREQ           915.5
#define TX_POWER            23
#define SMOKE_THRESHOLD     400   // Tune after calibration
#define MQ2_WARMUP_MS       30000 // 30s warm-up on boot
#define SAMPLE_INTERVAL_MS  1000  // Check sensor every 1 second
#define ALERT_REPEAT_MS     5000  // Re-send alert every 5s while smoke persists
#define CLEAR_REPEAT_MS     10000 // Re-send ALL CLEAR once after 10s to confirm

// ---------------------------------------------------------------
// [FAKE DATA] --- Fake sensor injection settings ---
// Set FAKE_SENSOR_ENABLED to false to revert to real sensor readings.
// ---------------------------------------------------------------
#define FAKE_SENSOR_ENABLED   true
#define FAKE_ALERT_VALUE      700   // Injected value above threshold (400) to trigger ALERT
#define FAKE_CLEAR_VALUE      100   // Injected value below threshold — returned to after alert
#define FAKE_CYCLE_MS         30000 // Every 30 seconds, inject one alert spike
#define FAKE_ALERT_DURATION_MS 5000 // How long the fake alert value lasts (5s)

unsigned long fakeLastCycleTime = 0;
bool          fakeAlertPhase    = false; // true = currently injecting alert value
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// PAYLOAD STRUCTURE — must match all other nodes exactly
// ---------------------------------------------------------------
struct Message {
  uint8_t nodeId;
  char    payload[40];
  uint8_t checksum;
};

RH_RF95        rf95(RFM95_CS, RFM95_INT);
SoftwareSerial m5Serial(SOFT_RX_PIN, SOFT_TX_PIN); // RX, TX

unsigned long lastSampleTime   = 0;
unsigned long lastTransmitTime = 0;

bool smokeActive  = false;
bool allClearSent = true;

uint8_t calculateChecksum(Message *msg);

// ---------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  m5Serial.begin(9600);

  // MQ2 warm-up
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
  Serial.print(F("  LoRa P2P Sender Node Ready. ID: "));
  Serial.println(MY_NODE_ID);
  Serial.println(F("  Alert-only mode: silent when clear."));
#if FAKE_SENSOR_ENABLED
  Serial.println(F("  *** FAKE SENSOR MODE ACTIVE ***"));
  Serial.print(F("  Alert spike every "));
  Serial.print(FAKE_CYCLE_MS / 1000);
  Serial.println(F("s, lasting "));
  Serial.print(FAKE_ALERT_DURATION_MS / 1000);
  Serial.println(F("s."));
#endif
  Serial.println(F("============================================"));

  // Let M5Stick know this node is ready
  m5Serial.print(F("READY,"));
  m5Serial.println(MY_NODE_ID);

  // [FAKE DATA] Initialise fake cycle timer after setup completes
  fakeLastCycleTime = millis();
}

// ---------------------------------------------------------------
// [FAKE DATA] Returns the raw sensor value to use this tick.
// If FAKE_SENSOR_ENABLED is false, always reads the real pin.
// ---------------------------------------------------------------
int getSmokeSensorValue() {
#if FAKE_SENSOR_ENABLED
  unsigned long now = millis();
  unsigned long elapsed = now - fakeLastCycleTime;

  if (!fakeAlertPhase && elapsed >= FAKE_CYCLE_MS) {
    // Time to start an alert spike
    fakeAlertPhase    = true;
    fakeLastCycleTime = now;
    Serial.println(F("[FAKE] Injecting ALERT value."));
    return FAKE_ALERT_VALUE;
  }

  if (fakeAlertPhase) {
    if (elapsed < FAKE_ALERT_DURATION_MS) {
      // Still within the alert window — keep injecting alert value
      return FAKE_ALERT_VALUE;
    } else {
      // Alert window expired — switch back to clear
      fakeAlertPhase    = false;
      fakeLastCycleTime = now;
      Serial.println(F("[FAKE] Alert window ended, returning CLEAR value."));
      return FAKE_CLEAR_VALUE;
    }
  }

  // Normal quiet phase — return clear value
  return FAKE_CLEAR_VALUE;

#else
  return analogRead(MQ2_PIN); // Real hardware reading
#endif
}

// ---------------------------------------------------------------
void loop() {
  if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = millis();

    // [FAKE DATA] Use getSmokeSensorValue() instead of analogRead() directly
    int  rawValue = getSmokeSensorValue();
    bool smokeNow = (rawValue >= SMOKE_THRESHOLD);

    // Always forward current reading to M5Stick
    sendToM5Stick(rawValue, smokeNow);

    // Transition: clear → smoke
    if (smokeNow && !smokeActive) {
      smokeActive  = true;
      allClearSent = false;
      Serial.println(F("!!! SMOKE DETECTED — sending alert !!!"));
      sendLoRa(rawValue, true);
      lastTransmitTime = millis();
    }

    // Smoke persisting — repeat alert every ALERT_REPEAT_MS
    else if (smokeNow && smokeActive) {
      if (millis() - lastTransmitTime >= ALERT_REPEAT_MS) {
        Serial.println(F("!!! SMOKE PERSISTS — resending alert !!!"));
        sendLoRa(rawValue, true);
        lastTransmitTime = millis();
      }
    }

    // Transition: smoke → clear
    else if (!smokeNow && smokeActive) {
      smokeActive = false;
      Serial.println(F("Smoke cleared — sending ALL CLEAR."));
      sendLoRa(rawValue, false);
      lastTransmitTime = millis();
    }

    // Still clear but final ALL CLEAR confirmation not sent yet
    else if (!smokeNow && !smokeActive && !allClearSent) {
      if (millis() - lastTransmitTime >= CLEAR_REPEAT_MS) {
        Serial.println(F("Resending ALL CLEAR confirmation."));
        sendLoRa(rawValue, false);
        lastTransmitTime = millis();
        allClearSent = true;
      }
    }

    // Truly clear — no LoRa transmission
    else {
      Serial.print(F("Clear. Raw: "));
      Serial.print(rawValue);
      Serial.println(F(" (not transmitted)"));
    }
  }
}

// ---------------------------------------------------------------
// Send packet over LoRa
// ---------------------------------------------------------------
void sendLoRa(int rawValue, bool isAlert) {
  float voltage = rawValue * (5.0 / 1023.0);

  Message msg;
  msg.nodeId = MY_NODE_ID;

  char voltStr[8];
  dtostrf(voltage, 4, 2, voltStr);

  snprintf(msg.payload, sizeof(msg.payload),
           "N%d,R%d,V%s,%s",
           MY_NODE_ID,
           rawValue,
           voltStr,
           isAlert ? "ALERT" : "CLEAR");

  // [SECURITY ADDED] encrypt payload BEFORE sending
  String originalPayload = String(msg.payload);
  String encryptedPayload = xorCipher(originalPayload);

  // [SECURITY TEST] Show plaintext vs encrypted
  Serial.println(F("------ SECURITY TEST (SENDER) ------"));
  Serial.print(F("Plain: "));
  Serial.println(originalPayload);

  Serial.print(F("Encrypted: "));
  Serial.println(encryptedPayload);
  Serial.println(F("-----------------------------------"));
  Serial.println(F("[INFO] Encrypted packet sent over LoRa"));

  // [SECURITY ADDED] safe copy with null termination
  strncpy(msg.payload, encryptedPayload.c_str(), sizeof(msg.payload) - 1);
  msg.payload[sizeof(msg.payload) - 1] = '\0';

  // [SECURITY ADDED] recompute checksum AFTER encryption
  msg.checksum = calculateChecksum(&msg);

  // send as usual (logic unchanged)
  rf95.send((uint8_t*)&msg, sizeof(msg));
  rf95.waitPacketSent();

  Serial.println(F("--- [TX] Transmitted ---"));
  Serial.print(F("  Raw ADC     : ")); Serial.print(rawValue);
                                        Serial.println(F(" / 1023"));
  Serial.print(F("  Voltage     : ")); Serial.print(voltage, 2);
                                        Serial.println(F(" V"));
  Serial.print(F("  Status sent : "));
  Serial.println(isAlert ? "*** SMOKE ALERT ***" : "ALL CLEAR");
}

// ---------------------------------------------------------------
// Forward own sensor reading to M5StickC Plus via UART
// Same format as LoRa payload for consistent parsing on M5Stick
// ---------------------------------------------------------------
void sendToM5Stick(int rawValue, bool isAlert) {
  float voltage = rawValue * (5.0 / 1023.0);

  char voltStr[8];
  dtostrf(voltage, 4, 2, voltStr);

  char uartStr[40];
  snprintf(uartStr, sizeof(uartStr),
           "N%d,R%d,V%s,%s",
           MY_NODE_ID,
           rawValue,
           voltStr,
           isAlert ? "ALERT" : "CLEAR");

  m5Serial.println(uartStr);
}

// ---------------------------------------------------------------
uint8_t calculateChecksum(Message *msg) {
  uint8_t checksum = 0;
  checksum ^= msg->nodeId;
  for (int i = 0; i < (int)strlen(msg->payload); i++) {
    checksum ^= (uint8_t)msg->payload[i];
  }
  return checksum;
}
