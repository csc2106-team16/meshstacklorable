#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <M5StickCPlus.h>

#define BLE_DEVICE_NAME "ENV-NODE-01"
#define MQ2_PIN 26
#define SMOKE_THRESHOLD 400
#define SAMPLE_INTERVAL_MS 2000

BLEAdvertising* pAdvertising = nullptr;
unsigned long lastBroadcastMs = 0;

int nodeId = 1;
int rawValue = 0;
float voltage = 0.0;
bool smokeDetected = false;
bool sensorValid = false;

// Manufacturer ID bytes
const uint8_t MANUFACTURER_ID_LOW  = 0xFF;
const uint8_t MANUFACTURER_ID_HIGH = 0xFF;

String smokeLabel(bool detected) {
  return detected ? "ALERT" : "CLEAR";
}

void readSensorData() {
  rawValue = analogRead(MQ2_PIN);

  // Simple sensor validity check
  // If disconnected, ADC may read 0, max, or unstable bad values.
  if (rawValue <= 5 || rawValue >= 1018) {
    sensorValid = false;
    voltage = 0.0;
    smokeDetected = false;
    return;
  }

  sensorValid = true;

  // M5StickC Plus / ESP32 is usually 3.3V logic
  voltage = rawValue * (3.3 / 1023.0);
  smokeDetected = (rawValue >= SMOKE_THRESHOLD);
}

String buildPayload() {
  // Format:
  // Normal: SMK|node|raw|voltage|status
  // Error : SMK|node|ERR|ERR|ERROR
  String payload = "SMK|";
  payload += String(nodeId);
  payload += "|";

  if (!sensorValid) {
    payload += "ERR|ERR|ERROR";
    return payload;
  }

  payload += String(rawValue);
  payload += "|";
  payload += String(voltage, 2);
  payload += "|";
  payload += smokeLabel(smokeDetected);

  return payload;
}

void updateDisplay() {
  M5.Lcd.fillRect(0, 20, 240, 120, BLACK);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.printf("Node : %d\n", nodeId);

  if (!sensorValid) {
    M5.Lcd.println("Sensor ERROR");
    M5.Lcd.println("Check wiring");
    return;
  }

  M5.Lcd.printf("Raw  : %d\n", rawValue);
  M5.Lcd.printf("Volt : %.2f V\n", voltage);
  M5.Lcd.printf("Stat : %s\n", smokeLabel(smokeDetected).c_str());
}

void updateAdvertisingPayload(const String& dataPayload) {
  BLEAdvertisementData advData;
  BLEAdvertisementData scanResponse;

  std::string manufacturerData;
  manufacturerData.push_back((char)MANUFACTURER_ID_LOW);
  manufacturerData.push_back((char)MANUFACTURER_ID_HIGH);
  manufacturerData += std::string(dataPayload.c_str());

  advData.setFlags(0x06);
  advData.setManufacturerData(String(manufacturerData.c_str()));

  scanResponse.setName(BLE_DEVICE_NAME);

  pAdvertising->stop();
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanResponse);
  pAdvertising->start();

  Serial.print("Broadcast payload: ");
  Serial.println(dataPayload);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Smoke BLE Server");

  pinMode(MQ2_PIN, INPUT);

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer* pServer = BLEDevice::createServer();
  (void)pServer;

  pAdvertising = BLEDevice::getAdvertising();

  readSensorData();
  updateDisplay();
  updateAdvertisingPayload(buildPayload());
  lastBroadcastMs = millis();
}

void loop() {
  M5.update();

  if (millis() - lastBroadcastMs >= SAMPLE_INTERVAL_MS) {
    readSensorData();
    updateDisplay();
    updateAdvertisingPayload(buildPayload());
    lastBroadcastMs = millis();
  }

  delay(50);
}
