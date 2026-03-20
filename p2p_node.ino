#include <SPI.h>
#include <RH_RF95.h>

// NODE IDENTITY — change this for each sender node (1 or 2)
#define MY_NODE_ID   1

// PIN DEFINITIONS
#define RFM95_CS    10
#define RFM95_RST    9
#define RFM95_INT    2
#define MQ2_PIN     A0

// RADIO & SENSOR SETTINGS
#define RF95_FREQ           915.5
#define TX_POWER            23
#define SMOKE_THRESHOLD     400   // Tune after calibration
#define MQ2_WARMUP_MS       30000 // 30s warm-up on boot
#define SAMPLE_INTERVAL_MS  10000  // Check sensor every 1 second
#define ALERT_REPEAT_MS     5000  // Re-send alert every 5s while smoke persists
#define CLEAR_REPEAT_MS     10000 // Re-send ALL CLEAR once after 10s to confirm


// PAYLOAD STRUCTURE — must match all other nodes exactly
struct Message {
  uint8_t nodeId;
  char    payload[40];
  uint8_t checksum;
};

RH_RF95 rf95(RFM95_CS, RFM95_INT);

unsigned long lastSampleTime   = 0;
unsigned long lastTransmitTime = 0;

// Track state to detect transitions
bool smokeActive  = false; // Is smoke currently detected?
bool allClearSent = true;  // Has ALL CLEAR been sent after smoke clears?

void setup() {
  Serial.begin(9600);

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
  Serial.println(F("============================================"));
}

void loop() {
  if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = millis();

    int  rawValue = analogRead(MQ2_PIN);
    bool smokeNow = (rawValue >= SMOKE_THRESHOLD);

    // Transition: clear → smoke
    if (smokeNow && !smokeActive) {
      smokeActive  = true;
      allClearSent = false;
      Serial.println(F("!!! SMOKE DETECTED — sending alert !!!"));
      sendMessage(rawValue, true);
      lastTransmitTime = millis();
    }

    // Smoke persisting — repeat alert every ALERT_REPEAT_MS
    else if (smokeNow && smokeActive) {
      if (millis() - lastTransmitTime >= ALERT_REPEAT_MS) {
        Serial.println(F("!!! SMOKE PERSISTS — resending alert !!!"));
        sendMessage(rawValue, true);
        lastTransmitTime = millis();
      }
    }

    // Transition: smoke → clear — send ALL CLEAR immediately
    else if (!smokeNow && smokeActive) {
      smokeActive = false;
      Serial.println(F("Smoke cleared — sending ALL CLEAR."));
      sendMessage(rawValue, false);
      lastTransmitTime = millis();
    }

    // Still clear but haven't sent a final ALL CLEAR confirmation yet
    else if (!smokeNow && !smokeActive && !allClearSent) {
      if (millis() - lastTransmitTime >= CLEAR_REPEAT_MS) {
        Serial.println(F("Resending ALL CLEAR confirmation."));
        sendMessage(rawValue, false);
        lastTransmitTime = millis();
        allClearSent = true; // Done — go silent again
      }
    }

    // Truly clear and confirmed — say nothing
    else {
      Serial.print(F("Clear. Raw: "));
      Serial.print(rawValue);
      Serial.println(F(" (not transmitted)"));
    }
  }
}

void sendMessage(int rawValue, bool isAlert) {
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

  msg.checksum = calculateChecksum(&msg);

  rf95.send((uint8_t*)&msg, sizeof(msg));
  rf95.waitPacketSent();

  Serial.println(F("--- [TX] Transmitted ---"));
  Serial.print(F("  Raw ADC     : ")); 
  Serial.print(rawValue);
  Serial.println(F(" / 1023"));
  Serial.print(F("  Voltage     : ")); 
  Serial.print(voltage, 2);
  Serial.println(F(" V"));
  Serial.print(F("  Status sent : "));
  Serial.println(isAlert ? "*** SMOKE ALERT ***" : "ALL CLEAR");
}

uint8_t calculateChecksum(Message *msg) {
  uint8_t checksum = 0;
  checksum ^= msg->nodeId;
  for (int i = 0; i < (int)strlen(msg->payload); i++) {
    checksum ^= (uint8_t)msg->payload[i];
  }
  return checksum;
}
