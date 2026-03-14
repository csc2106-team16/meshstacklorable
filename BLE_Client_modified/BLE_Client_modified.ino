#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <M5Unified.h>

// =====================================================
// Environmental BLE Listener / Scanner
// - listens to BLE advertisements from nearby env nodes
// - no connection required
// - easy match to simple mobile-scanner workflow
// =====================================================

#define TARGET_DEVICE_NAME  "ENV-NODE-01"
#define SCAN_TIME_SECONDS   5

BLEScan* pBLEScan = nullptr;
unsigned long lastScanStart = 0;

String latestNode = "-";
String latestAlert = "-";
String latestTemp = "-";
String latestHum = "-";
String latestAqi = "-";
int latestRssi = -999;

String alertLabelFromLevel(const String& levelStr) {
  if (levelStr == "0") return "SAFE";
  if (levelStr == "1") return "CAUTION";
  if (levelStr == "2") return "HAZARD";
  return "UNKNOWN";
}

bool splitPayload(const String& payload, String parts[], int expectedParts) {
  int start = 0;
  int index = 0;

  while (index < expectedParts - 1) {
    int sep = payload.indexOf('|', start);
    if (sep < 0) return false;
    parts[index++] = payload.substring(start, sep);
    start = sep + 1;
  }
  parts[index] = payload.substring(start);
  return index == expectedParts - 1;
}

void updateDisplay() {
  M5.Lcd.fillRect(0, 20, 160, 100, BLACK);
  M5.Lcd.setCursor(0, 20, 2);
  M5.Lcd.printf("Node : %s\n", latestNode.c_str());
  M5.Lcd.printf("Alert: %s\n", latestAlert.c_str());
  M5.Lcd.printf("Temp : %s C\n", latestTemp.c_str());
  M5.Lcd.printf("Hum  : %s %%\n", latestHum.c_str());
  M5.Lcd.printf("AQI  : %s\n", latestAqi.c_str());
  M5.Lcd.printf("RSSI : %d dBm\n", latestRssi);
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    std::string devName = advertisedDevice.getName();

    if (devName.empty()) {
      return;
    }

    if (String(devName.c_str()) != TARGET_DEVICE_NAME) {
      return;
    }

    Serial.println("\nTarget environmental node found.");
    Serial.print("RSSI: ");
    Serial.println(advertisedDevice.getRSSI());

    latestRssi = advertisedDevice.getRSSI();

    if (!advertisedDevice.haveManufacturerData()) {
      Serial.println("No manufacturer data found.");
      return;
    }

    std::string mfg = advertisedDevice.getManufacturerData();
    if (mfg.length() < 3) {
      Serial.println("Manufacturer data too short.");
      return;
    }

    // Drop first 2 bytes = manufacturer ID
    String payload = String(mfg.substr(2).c_str());
    Serial.print("Payload: ");
    Serial.println(payload);

    String parts[6];
    if (!splitPayload(payload, parts, 6)) {
      Serial.println("Payload parse failed.");
      return;
    }

    // Expected: ENV|node|level|temp|humidity|aqi
    if (parts[0] != "ENV") {
      Serial.println("Unexpected payload prefix.");
      return;
    }

    latestNode = parts[1];
    latestAlert = alertLabelFromLevel(parts[2]);
    latestTemp = parts[3];
    latestHum = parts[4];
    latestAqi = parts[5];

    updateDisplay();
  }
};

void startScan() {
  Serial.println("Starting BLE scan...");
  pBLEScan->start(SCAN_TIME_SECONDS, false);
  pBLEScan->clearResults();
  lastScanStart = millis();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.println("Env BLE Listener");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  updateDisplay();
  startScan();
}

void loop() {
  M5.update();

  // Button A = restart scan manually
  if (M5.BtnA.wasPressed()) {
    startScan();
  }

  // Restart scanning periodically so it keeps listening continuously
  if (millis() - lastScanStart > (SCAN_TIME_SECONDS * 1000UL + 1000UL)) {
    startScan();
  }

  delay(50);
}
