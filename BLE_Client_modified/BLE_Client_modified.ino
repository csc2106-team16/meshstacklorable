#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <M5StickCPlus.h>

#define TARGET_DEVICE_NAME "ENV-NODE-01"
#define SCAN_TIME_SECONDS 5
#define DATA_TIMEOUT 7000

BLEScan* pBLEScan = nullptr;
unsigned long lastScanStart = 0;
unsigned long lastDataReceived = 0;

String latestNode = "-";
String latestRaw = "-";
String latestVoltage = "-";
String latestStatus = "NO DATA";
int latestRssi = -999;

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

void resetLatestValues() {
  latestNode = "-";
  latestRaw = "-";
  latestVoltage = "-";
  latestRssi = -999;
}

void updateDisplay() {
  M5.Lcd.fillRect(0, 20, 240, 120, BLACK);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.printf("Node : %s\n", latestNode.c_str());
  M5.Lcd.printf("Raw  : %s\n", latestRaw.c_str());
  M5.Lcd.printf("Volt : %s V\n", latestVoltage.c_str());
  M5.Lcd.printf("Stat : %s\n", latestStatus.c_str());

  if (latestRssi != -999) {
    M5.Lcd.printf("RSSI : %d dBm\n", latestRssi);
  } else {
    M5.Lcd.printf("RSSI : ---\n");
  }
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    String devName = advertisedDevice.getName().c_str();

    if (devName.length() == 0) return;
    if (devName != TARGET_DEVICE_NAME) return;

    Serial.println("\nTarget smoke node found.");
    Serial.print("RSSI: ");
    Serial.println(advertisedDevice.getRSSI());

    latestRssi = advertisedDevice.getRSSI();

    if (!advertisedDevice.haveManufacturerData()) {
      Serial.println("No manufacturer data found.");
      latestStatus = "NO PAYLOAD";
      resetLatestValues();
      updateDisplay();
      return;
    }

    String mfgStd = advertisedDevice.getManufacturerData();
    String mfg = String(mfgStd.c_str());

    if (mfg.length() < 3) {
      Serial.println("Manufacturer data too short.");
      latestStatus = "BAD DATA";
      resetLatestValues();
      updateDisplay();
      return;
    }

    // first 2 bytes are manufacturer ID
    String payload = mfg.substring(2);

    Serial.print("Payload: ");
    Serial.println(payload);

    String parts[5];
    if (!splitPayload(payload, parts, 5)) {
      Serial.println("Payload parse failed.");
      latestStatus = "PARSE ERR";
      resetLatestValues();
      updateDisplay();
      return;
    }

    // Expected: SMK|node|raw|voltage|status
    if (parts[0] != "SMK") {
      Serial.println("Unexpected payload prefix.");
      latestStatus = "BAD PREFIX";
      resetLatestValues();
      updateDisplay();
      return;
    }

    lastDataReceived = millis();
    latestNode = parts[1];

    if (parts[2] == "ERR" || parts[3] == "ERR" || parts[4] == "ERROR") {
      latestRaw = "-";
      latestVoltage = "-";
      latestStatus = "SENSOR ERR";
      updateDisplay();
      return;
    }

    latestRaw = parts[2];
    latestVoltage = parts[3];
    latestStatus = parts[4];

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
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Smoke BLE Client");

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

  if (M5.BtnA.wasPressed()) {
    startScan();
  }

  if (millis() - lastScanStart > (SCAN_TIME_SECONDS * 1000UL + 1000UL)) {
    startScan();
  }

  if (lastDataReceived != 0 && millis() - lastDataReceived > DATA_TIMEOUT) {
    latestStatus = "NO SIGNAL";
    updateDisplay();
  }

  delay(50);
}
