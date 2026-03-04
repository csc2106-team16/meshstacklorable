#include <RH_RF95.h>
#include <SoftwareSerial.h>

// Shield-ingrained pins (hardware fixed — do not change):
//   D2  → DIO0 interrupt
//   D5  → DIO1
//   D6  → DIO2
//   D7  → LORA_RST
//   D10 → NSS (chip select)
//   SPI bus: MOSI=11, MISO=12, SCK=13
// SoftwareSerial on 8/9 — the only two clean free pins left.
#define RFM95_CS  10
#define RFM95_INT  2
#define RFM95_RST  7
#define MQ2_PIN   A0
#define RF95_FREQ 915.0   // MHz — change to 868.0 for EU / 433.0 for Asia

RH_RF95 rf95(RFM95_CS, RFM95_INT);

// RX=8, TX=9  (D7 is LORA_RST; D10-D13 are SPI — both groups off-limits)
SoftwareSerial espSerial(8, 9);

bool wifiDownMode = false;
bool loraReady    = false;   // false until rf95.init() succeeds
unsigned long lastSend = 0;
const unsigned long interval = 2000;


void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  pinMode(MQ2_PIN, INPUT);

  // Hard-reset the module before init (D7 = LORA_RST, shield-ingrained)
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  if (!rf95.init()) {
    // Flag it and carry on — UART/WiFi path still works without LoRa
    Serial.println("LoRa init FAILED — running UART-only until reboot");
    loraReady = false;
  } else {
    rf95.setFrequency(RF95_FREQ);   // rarely fails after good init; no halt needed
    rf95.setTxPower(13, false);     // 13 dBm — safe indoor default (max 23)
    loraReady = true;
    Serial.print("LoRa ready at ");
    Serial.print(RF95_FREQ);
    Serial.println(" MHz");
  }

  Serial.println("Arduino Ready");
}

void loop() {

  // If ESP32 sends something
  if (espSerial.available()) {
    String msg = espSerial.readStringUntil('\n');
    msg.trim();

    if (msg == "WIFI_DOWN") {
      wifiDownMode = true;
      Serial.println("Switched to LoRa Mode");
    }

    if (msg == "WIFI_UP") {
      wifiDownMode = false;
      Serial.println("Switched to UART Mode");
    }

    Serial.print("From ESP32: ");
    Serial.println(msg);
  }


  if (millis() - lastSend > interval) {
    lastSend = millis();

    int smokeValue = analogRead(MQ2_PIN);

    String payload = "C|SMOKE:" + String(smokeValue);

    if (!wifiDownMode) {
      espSerial.println(payload);
      Serial.println("Sent via UART: " + payload);
    }
    else {
      if (loraReady) {
        sendViaLora(payload);
      } else {
        Serial.println("LoRa unavailable — packet dropped");
      }
    }
  }

  // If PC sends something
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    espSerial.println(msg);
  }
}

void sendViaLora(String data) {
  uint8_t buffer[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t len = data.length();

  data.getBytes(buffer, len +1);

  rf95.send(buffer, len);
  rf95.waitPacketSent();

  Serial.println("LoRa packet sent: " + data);
}