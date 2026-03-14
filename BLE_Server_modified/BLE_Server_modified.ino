#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <M5Unified.h>

// =====================================================
// Environmental Monitoring BLE Beacon + Optional GATT
// - Broadcast hazard alerts to nearby phones without Internet
// - Keep a connectable GATT service for richer debugging/demo
// =====================================================

#define BLE_DEVICE_NAME       "ENV-NODE-01"
#define NODE_ID               1
#define UPDATE_INTERVAL_MS    5000
#define CONNECTABLE_MODE      true

// Custom service/characteristic UUIDs
#define ENV_SERVICE_UUID      "7f6d0001-4b5a-4c7d-9e10-112233445566"
#define ALERT_CHAR_UUID       "7f6d0002-4b5a-4c7d-9e10-112233445566"
#define DATA_CHAR_UUID        "7f6d0003-4b5a-4c7d-9e10-112233445566"

// Company ID 0xFFFF = test/manufacturer specific for demo
#define MANUFACTURER_ID_LOW   0xFF
#define MANUFACTURER_ID_HIGH  0xFF

BLEServer* pServer = nullptr;
BLEAdvertising* pAdvertising = nullptr;
BLECharacteristic* pAlertCharacteristic = nullptr;
BLECharacteristic* pDataCharacteristic = nullptr;

bool deviceConnected = false;
unsigned long lastUpdate = 0;

float temperatureC = 28.0f;
float humidityPct = 70.0f;
int airQualityIndex = 45;

// 0 = SAFE, 1 = CAUTION, 2 = HAZARD
uint8_t alertLevel = 0;
String alertText = "SAFE";

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    deviceConnected = true;
    Serial.println("BLE client connected.");
  }

  void onDisconnect(BLEServer* server) override {
    deviceConnected = false;
    Serial.println("BLE client disconnected.");
    BLEDevice::startAdvertising();
    Serial.println("Advertising restarted.");
  }
};

void updateEnvironmentalData() {
  // Replace these simulated values with real sensor reads later.
  // Example future sources:
  // temperatureC = sht31.readTemperature();
  // humidityPct = sht31.readHumidity();
  // airQualityIndex = pm25_to_aqi(pm25SensorValue);

  temperatureC += (random(-5, 6) / 10.0f);   // +/- 0.5C
  humidityPct += (random(-10, 11) / 10.0f);  // +/- 1.0%
  airQualityIndex += random(-8, 9);          // +/- 8

  if (temperatureC < 24.0f) temperatureC = 24.0f;
  if (temperatureC > 36.0f) temperatureC = 36.0f;
  if (humidityPct < 45.0f) humidityPct = 45.0f;
  if (humidityPct > 95.0f) humidityPct = 95.0f;
  if (airQualityIndex < 0) airQualityIndex = 0;
  if (airQualityIndex > 250) airQualityIndex = 250;

  if (airQualityIndex >= 151 || temperatureC >= 34.0f) {
    alertLevel = 2;
    alertText = "HAZARD";
  } else if (airQualityIndex >= 101 || temperatureC >= 31.0f || humidityPct >= 85.0f) {
    alertLevel = 1;
    alertText = "CAUTION";
  } else {
    alertLevel = 0;
    alertText = "SAFE";
  }
}

String buildDataPayload() {
  // Easy to inspect in nRF Connect / LightBlue / custom Kotlin app
  // Format: ENV|node|level|temp|humidity|aqi
  char payload[80];
  snprintf(payload, sizeof(payload), "ENV|%d|%u|%.1f|%.1f|%d",
           NODE_ID, alertLevel, temperatureC, humidityPct, airQualityIndex);
  return String(payload);
}

String buildAlertText() {
  char message[96];
  snprintf(message, sizeof(message), "Node %d %s | Temp %.1fC | Hum %.1f%% | AQI %d",
           NODE_ID, alertText.c_str(), temperatureC, humidityPct, airQualityIndex);
  return String(message);
}

void updateAdvertisingPayload(const String& dataPayload) {
  BLEAdvertisementData advData;
  BLEAdvertisementData scanResponse;

  // Manufacturer specific data:
  // [0xFF,0xFF][ENV|node|level|temp|humidity|aqi]
  std::string manufacturerData;
  manufacturerData.push_back((char)MANUFACTURER_ID_LOW);
  manufacturerData.push_back((char)MANUFACTURER_ID_HIGH);
  manufacturerData += std::string(dataPayload.c_str());

  advData.setFlags(0x06); // General discoverable + BR/EDR not supported
  advData.setName(BLE_DEVICE_NAME);
  advData.setManufacturerData(String(manufacturerData.c_str()));
  advData.setCompleteServices(BLEUUID(ENV_SERVICE_UUID));

  scanResponse.setName(BLE_DEVICE_NAME);

  pAdvertising->stop();
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanResponse);
  pAdvertising->start();
}

void updateDisplay() {
  M5.Lcd.fillRect(0, 20, 160, 90, BLACK);
  M5.Lcd.setCursor(0, 20, 2);
  M5.Lcd.printf("Node: %d\n", NODE_ID);
  M5.Lcd.printf("Alert: %s\n", alertText.c_str());
  M5.Lcd.printf("Temp: %.1f C\n", temperatureC);
  M5.Lcd.printf("Hum : %.1f %%\n", humidityPct);
  M5.Lcd.printf("AQI : %d\n", airQualityIndex);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(micros());

  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.println("Env BLE Beacon");

  BLEDevice::init(BLE_DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(ENV_SERVICE_UUID);

  pAlertCharacteristic = pService->createCharacteristic(
    ALERT_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pAlertCharacteristic->addDescriptor(new BLE2902());

  pDataCharacteristic = pService->createCharacteristic(
    DATA_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pDataCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(ENV_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  updateEnvironmentalData();
  String dataPayload = buildDataPayload();
  String alertPayload = buildAlertText();

  pAlertCharacteristic->setValue(alertPayload.c_str());
  pDataCharacteristic->setValue(dataPayload.c_str());
  updateAdvertisingPayload(dataPayload);
  updateDisplay();

  Serial.println("Environmental BLE beacon started.");
  Serial.println(dataPayload);
}

void loop() {
  M5.update();

  // Button A: force hazard for demo purposes
  if (M5.BtnA.wasPressed()) {
    airQualityIndex = 180;
    temperatureC = 35.0f;
    humidityPct = 88.0f;
    alertLevel = 2;
    alertText = "HAZARD";

    String dataPayload = buildDataPayload();
    String alertPayload = buildAlertText();

    pAlertCharacteristic->setValue(alertPayload.c_str());
    pDataCharacteristic->setValue(dataPayload.c_str());
    if (deviceConnected) {
      pAlertCharacteristic->notify();
      pDataCharacteristic->notify();
    }
    updateAdvertisingPayload(dataPayload);
    updateDisplay();
    Serial.println("Manual hazard triggered.");
    Serial.println(dataPayload);
  }

  if (millis() - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = millis();

    updateEnvironmentalData();
    String dataPayload = buildDataPayload();
    String alertPayload = buildAlertText();

    pAlertCharacteristic->setValue(alertPayload.c_str());
    pDataCharacteristic->setValue(dataPayload.c_str());

    if (deviceConnected) {
      pAlertCharacteristic->notify();
      pDataCharacteristic->notify();
    }

    updateAdvertisingPayload(dataPayload);
    updateDisplay();

    Serial.println("Updated BLE advertisement + GATT data:");
    Serial.println(dataPayload);
  }

  delay(50);
}
