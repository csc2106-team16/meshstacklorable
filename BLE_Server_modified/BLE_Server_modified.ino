#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <M5StickCPlus.h>

// ====================================================
// Environmental BLE Broadcaster / Server
// - broadcasts environmental data in manufacturer data
// - no client connection required
// ====================================================

#define BLE_DEVICE_NAME "ENV-NODE-01"

// Fake sample values for testing
int nodeId = 1;
int alertLevel = 1;     // 0 = SAFE, 1 = CAUTION, 2 = HAZARD
float temperature = 29.5;
float humidity = 67.2;
int aqi = 84;

BLEAdvertising* pAdvertising = nullptr;
unsigned long lastBroadcastMs = 0;
const unsigned long BROADCAST_INTERVAL_MS = 2000;

// Manufacturer ID bytes (arbitrary test values)
const uint8_t MANUFACTURER_ID_LOW  = 0xFF;
const uint8_t MANUFACTURER_ID_HIGH = 0xFF;

String alertLabel(int level) {
  if (level == 0) return "SAFE";
  if (level == 1) return "CAUTION";
  if (level == 2) return "HAZARD";
  return "UNKNOWN";
}

String buildPayload() {
  // Format: ENV|node|level|temp|humidity|aqi
  String payload = "ENV|";
  payload += String(nodeId);
  payload += "|";
  payload += String(alertLevel);
  payload += "|";
  payload += String(temperature, 1);
  payload += "|";
  payload += String(humidity, 1);
  payload += "|";
  payload += String(aqi);

  return payload;
}

void updateDisplay() {
  M5.Lcd.fillRect(0, 20, 240, 120, BLACK);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.printf("Node : %d\n", nodeId);
  M5.Lcd.printf("Alert: %s\n", alertLabel(alertLevel).c_str());
  M5.Lcd.printf("Temp : %.1f C\n", temperature);
  M5.Lcd.printf("Hum  : %.1f %%\n", humidity);
  M5.Lcd.printf("AQI  : %d\n", aqi);
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

  // Put device name in scan response to save advertisement space
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
  M5.Lcd.println("Env BLE Server");

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer* pServer = BLEDevice::createServer();
  (void)pServer;  // not used directly, but keeps BLE server initialized

  pAdvertising = BLEDevice::getAdvertising();

  updateDisplay();
  updateAdvertisingPayload(buildPayload());
  lastBroadcastMs = millis();
}

void loop() {
  M5.update();

  // Button A cycles alert level
  if (M5.BtnA.wasPressed()) {
    alertLevel = (alertLevel + 1) % 3;
    updateDisplay();
    updateAdvertisingPayload(buildPayload());
    lastBroadcastMs = millis();
  }

  // Optional: simulate changing values over time
  if (millis() - lastBroadcastMs >= BROADCAST_INTERVAL_MS) {
    temperature += 0.1;
    if (temperature > 35.0) temperature = 29.5;

    humidity += 0.2;
    if (humidity > 80.0) humidity = 67.2;

    aqi += 1;
    if (aqi > 120) aqi = 84;

    updateDisplay();
    updateAdvertisingPayload(buildPayload());
    lastBroadcastMs = millis();
  }

  delay(50);
}
