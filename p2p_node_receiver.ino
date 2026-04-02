#include <SPI.h>
#include <RH_RF95.h>

// [SECURITY ADDED] --- LoRa lightweight security setup ---
const uint8_t LORA_KEY = 0x5A;

String xorCipher(const String& input) {
  String out = input;
  for (size_t i = 0; i < out.length(); i++) {
    out[i] = out[i] ^ LORA_KEY;
  }
  return out;
}

// NODE IDENTITY — change this for each node (1 to 5)
#define MY_NODE_ID   3        // Change to 3, 4, or 5 for receiver nodes

// PIN DEFINITIONS
#define RFM95_CS    10
#define RFM95_RST    9
#define RFM95_INT    2


// RADIO SETTINGS
#define RF95_FREQ   915.5
#define TX_POWER    23


// PAYLOAD STRUCTURE — must match sender nodes exactly
struct Message {
  uint8_t nodeId;
  char    payload[40];
  uint8_t checksum;
};

RH_RF95 rf95(RFM95_CS, RFM95_INT);

void setup() {
  Serial.begin(9600);

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
  Serial.print(F("  LoRa P2P Receiver Node Ready. ID: "));
  Serial.println(MY_NODE_ID);
  Serial.println(F("  Listening for sensor data..."));
  Serial.println(F("============================================"));
}

void loop() {
  if (rf95.available()) {
    receivePeerData();
  }
}

void receivePeerData() {
  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);

  if (!rf95.recv(buf, &len)) return;

  Message* incoming = (Message*)buf;

  // Verify checksum
  uint8_t calcCheck = calculateChecksum(incoming);
  if (calcCheck != incoming->checksum) {
    Serial.println(F("[ERROR] Checksum mismatch — packet dropped."));
    return;
  }

  // [SECURITY ADDED] decrypt payload AFTER checksum verification
  String encryptedPayload = String(incoming->payload);
  String decryptedPayload = xorCipher(encryptedPayload);

  // [SECURITY ADDED] overwrite payload with decrypted text
  strncpy(incoming->payload, decryptedPayload.c_str(), sizeof(incoming->payload) - 1);
  incoming->payload[sizeof(incoming->payload) - 1] = '\0';

  // Parse payload: "N<id>,R<raw>,V<voltage>,<OK|ALERT>"
  char payloadCopy[40];
  strncpy(payloadCopy, incoming->payload, sizeof(payloadCopy));
  payloadCopy[sizeof(payloadCopy) - 1] = '\0';

  char* nodeToken   = strtok(payloadCopy, ",");
  char* rawToken    = strtok(NULL, ",");
  char* voltToken   = strtok(NULL, ",");
  char* statusToken = strtok(NULL, ",");

  int   nodeId  = nodeToken  ? atoi(nodeToken  + 1) : incoming->nodeId;
  int   rawVal  = rawToken   ? atoi(rawToken   + 1) : -1;
  float voltage = voltToken  ? atof(voltToken  + 1) : -1.0;
  bool isAlert = statusToken && strcmp(statusToken, "ALERT") == 0;
  bool isClear = statusToken && strcmp(statusToken, "CLEAR") == 0;

  // Print to Serial Monitor
  Serial.println(F("--------------------------------------------"));
  Serial.print(F("  From Node   : ")); 
  Serial.println(nodeId);
  Serial.print(F("  RSSI (dBm)  : ")); 
  Serial.println(rf95.lastRssi());
  Serial.print(F("  Raw ADC     : ")); 
  Serial.print(rawVal);
  Serial.println(F(" / 1023"));
  Serial.print(F("  Voltage     : ")); 
  Serial.print(voltage, 2);
  Serial.println(F(" V"));
  Serial.print(F("  Smoke Status: "));
  if (isAlert) {
    Serial.println(F("*** SMOKE ALERT ***"));
  } else if (isClear) {
    Serial.println(F("ALL CLEAR — smoke has subsided."));
  } else {
    Serial.println(F("Unknown status."));
  }
  Serial.println(F("--------------------------------------------"));
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
