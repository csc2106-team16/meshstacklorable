#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <M5StickCPlus.h>

#define TARGET_DEVICE_NAME "ENV-NODE-01"

// Scan behavior
#define SCAN_TIME_SECONDS 2       // scan window length
#define SCAN_INTERVAL_MS 3000     // start a new scan every 3s
#define DATA_TIMEOUT_MS 8000      // if no packet for 8s => NO SIGNAL

BLEScan* pBLEScan = nullptr;

unsigned long lastScanStarted = 0;
unsigned long lastDataReceived = 0;
bool scanRunning = false;

// Latest retained readings
String latestNode = "-";
String latestRaw = "-";
String latestVoltage = "-";
String latestServerStat = "NO DATA";   // ALERT / CLEAR / ERROR from server
String latestScanStat = "IDLE";        // SCANNING / WAITING / NO SIGNAL / IDLE
int latestRssi = -999;

bool hasValidPacket = false;
String lastPayload = "";

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
  M5.Lcd.fillRect(0, 20, 240, 135, BLACK);
  M5.Lcd.setCursor(0, 20);

  M5.Lcd.printf("Node : %s\n", latestNode.c_str());
  M5.Lcd.printf("Raw  : %s\n", latestRaw.c_str());
  M5.Lcd.printf("Volt : %s V\n", latestVoltage.c_str());
  M5.Lcd.printf("Stat : %s\n", latestServerStat.c_str());
  M5.Lcd.printf("Scan : %s\n", latestScanStat.c_str());

  if (latestRssi != -999) {
    M5.Lcd.printf("RSSI : %d dBm\n", latestRssi);
  } else {
    M5.Lcd.printf("RSSI : ---\n");
  }
}

void processPayload(const String& payload, int rssi) {
  Serial.print("Payload: ");
  Serial.println(payload);

  String parts[5];
  if (!splitPayload(payload, parts, 5)) {
    Serial.println("Payload parse failed.");
    return;
  }

  // Expected format: SMK|node|raw|voltage|status
  if (parts[0] != "SMK") {
    Serial.println("Unexpected payload prefix.");
    return;
  }

  lastDataReceived = millis();
  latestRssi = rssi;
  latestNode = parts[1];
  latestScanStat = "WAITING";

  if (parts[2] == "ERR" || parts[3] == "ERR" || parts[4] == "ERROR") {
    latestRaw = "-";
    latestVoltage = "-";
    latestServerStat = "ERROR";
    hasValidPacket = true;
    updateDisplay();
    return;
  }

  latestRaw = parts[2];
  latestVoltage = parts[3];
  latestServerStat = parts[4];   // ALERT or CLEAR from server
  hasValidPacket = true;
  updateDisplay();
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    String devName = advertisedDevice.getName().c_str();

    if (devName.length() == 0) return;
    if (devName != TARGET_DEVICE_NAME) return;
    if (!advertisedDevice.haveManufacturerData()) return;

    latestRssi = advertisedDevice.getRSSI();

    String mfgStd = advertisedDevice.getManufacturerData();
    String mfg = String(mfgStd.c_str());

    // first 2 bytes are manufacturer ID
    if (mfg.length() < 3) return;

    String payload = mfg.substring(2);

    // Ignore duplicate packets so screen does not keep refreshing
    if (payload == lastPayload) {
      lastDataReceived = millis();
      latestScanStat = "WAITING";
      return;
    }

    lastPayload = payload;
    processPayload(payload, advertisedDevice.getRSSI());
  }
};

void startScan() {
  if (scanRunning) return;

  Serial.println("Starting BLE scan...");
  latestScanStat = "SCANNING";
  updateDisplay();

  pBLEScan->clearResults();
  pBLEScan->start(SCAN_TIME_SECONDS, false);  // blocking scan for 2 seconds
  scanRunning = true;
  lastScanStarted = millis();

  // once start() returns, scan has ended
  scanRunning = false;

  if (hasValidPacket) {
    latestScanStat = "WAITING";
  } else {
    latestScanStat = "IDLE";
  }

  updateDisplay();
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

  // Manual rescan
  if (M5.BtnA.wasPressed()) {
    startScan();
  }

  // Periodic scanning, not continuous
  if (!scanRunning && millis() - lastScanStarted >= SCAN_INTERVAL_MS) {
    startScan();
  }

  // If no new data comes for a while, keep previous readings but mark signal lost
  if (hasValidPacket && (millis() - lastDataReceived > DATA_TIMEOUT_MS)) {
    latestScanStat = "NO SIGNAL";
    updateDisplay();
  }

  delay(50);
}
