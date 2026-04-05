/*
 * M5StickC Plus — Integrated Air Quality Monitor (SENDER NODE 1)
 * Combines: WiFi/MQTT (TLS via gateway) + BLE Broadcasting + LoRa Fallback
 *
 * THIS NODE HAS A SENSOR — it publishes data to MQTT and BLE,
 * and receives data from its Arduino via UART.
 *
 * UART COMMANDS (M5Stick → Arduino):
 *   "LORA,ON\n"   — tells Arduino to start sending LoRa packets
 *   "LORA,OFF\n"  — tells Arduino to stop sending LoRa packets
 *
 * UART DATA (Arduino → M5Stick):
 *   "READY,<id>\n"
 *   "N<id>,R<raw>,V<voltage>,<ALERT|CLEAR>\n"
 *
 * UART Wiring (M5StickC Plus ↔ Arduino):
 *   Grove Port PIN 32 (RX) ← Arduino Pin 4 (TX)
 *   Grove Port PIN 33 (TX) → Arduino Pin 5 (RX)
 *   GND                    ↔ Arduino GND
 *
 * Library required:
 *   M5StickCPlus, NimBLE-Arduino, WiFi, PubSubClient
 */

#include <M5StickCPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NimBLEDevice.h>
#include "gateway.h"

// ===============================================================
// CONFIGURATION — CHANGE THESE
// ===============================================================

// Node identity
#define MY_NODE_ID        1
#define BLE_DEVICE_NAME   "ENV-NODE-01"

// WiFi credentials
#define WIFI_SSID         "iPhone"
#define WIFI_PASSWORD     "MIJAWIFI78"

// ===============================================================
// TIMING
// ===============================================================
#define UART_BAUD               9600
#define SCREEN_W                240
#define SCREEN_H                135
#define BLE_UPDATE_INTERVAL_MS  2000
#define MQTT_UPDATE_INTERVAL_MS 2000
#define WIFI_CHECK_INTERVAL_MS  5000
#define WIFI_RECONNECT_TIMEOUT  5000
#define LORA_TIMEOUT_MS         15000
#define SCREEN_REFRESH_MS       2000

// ===============================================================
// OPERATING MODES
// ===============================================================
enum OperatingMode {
  MODE_WIFI,
  MODE_LORA
};

OperatingMode currentMode = MODE_WIFI;
bool loraCommandSent = false;

// ===============================================================
// SENSOR DATA (shared between modes)
// ===============================================================
int   myNodeId      = MY_NODE_ID;
int   fromNodeId    = -1;
int   rawValue      = 0;
float voltage       = 0.0;
bool  smokeDetected = false;
bool  sensorValid   = false;
bool  dataReceived  = false;

String incomingBuffer = "";

// ===============================================================
// WIFI / MQTT
// ===============================================================
unsigned long lastMqttUpdate  = 0;
unsigned long lastWifiCheck   = 0;
unsigned long wifiLostTime    = 0;
bool wifiConnected            = false;
bool mqttConnected            = false;

// ===============================================================
// BLE
// ===============================================================
NimBLEAdvertising* pAdvertising = nullptr;
unsigned long lastBleUpdate = 0;

const uint8_t MANUFACTURER_ID_LOW  = 0xFF;
const uint8_t MANUFACTURER_ID_HIGH = 0xFF;

// ===============================================================
// LORA DISPLAY
// ===============================================================
unsigned long lastLoraDataTime = 0;
bool loraWaiting = true;

// ===============================================================
// SCREEN STATE
// ===============================================================
OperatingMode lastDrawnMode = MODE_WIFI;
bool forceRedraw = true;
unsigned long lastScreenRefresh = 0;


// ###############################################################
//  FORWARD DECLARATION
// ###############################################################
void mqttCallback(char* topic, byte* payload, unsigned int length);


// ###############################################################
//  SETUP
// ###############################################################
void setup() {
  Serial.begin(115200);
  delay(500);

  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 30);
  M5.Lcd.println("Initializing...");

  // UART to/from Arduino (RX=32, TX=33 on Grove port)
  Serial2.begin(UART_BAUD, SERIAL_8N1, 32, 33);

  // WiFi (via gateway TLS layer)
  setupWiFi();

  // MQTT (via gateway TLS layer)
  connectMQTT();

  // Register MQTT callback for receiving data from other nodes
  client.setCallback(mqttCallback);

  // BLE
  setupBLE();

  // Tell Arduino to keep LoRa silent (WiFi is primary)
  sendArduinoCommand("LORA,OFF");

  forceRedraw = true;
  lastWifiCheck = millis();
}


// ###############################################################
//  MAIN LOOP
// ###############################################################
void loop() {
  M5.update();

  // --- Read UART from Arduino (always, regardless of mode) ---
  readUART();

  // --- Periodic WiFi health check ---
  if (millis() - lastWifiCheck >= WIFI_CHECK_INTERVAL_MS) {
    lastWifiCheck = millis();
    checkWiFiHealth();
  }

  // --- Mode-specific logic ---
  if (currentMode == MODE_WIFI) {
    loopWiFiMode();
  } else {
    loopLoRaMode();
  }

  delay(50);
}


// ###############################################################
//  UART — READ FROM ARDUINO
// ###############################################################
void readUART() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      incomingBuffer.trim();
      if (incomingBuffer.length() > 0) {
        parseUART(incomingBuffer);
      }
      incomingBuffer = "";
    } else {
      incomingBuffer += c;
    }
  }
}

void parseUART(String data) {
  // Handle READY message
  if (data.startsWith("READY,")) {
    myNodeId = data.substring(6).toInt();
    Serial.print("Arduino ready, node: ");
    Serial.println(myNodeId);
    return;
  }

  // Parse: "N<id>,R<raw>,V<voltage>,<ALERT|CLEAR>"
  int n1 = data.indexOf(',');
  int n2 = data.indexOf(',', n1 + 1);
  int n3 = data.indexOf(',', n2 + 1);

  if (n1 < 0 || n2 < 0 || n3 < 0) return;

  fromNodeId    = data.substring(1, n1).toInt();
  rawValue      = data.substring(n1 + 2, n2).toInt();
  voltage       = data.substring(n2 + 2, n3).toFloat();
  String status = data.substring(n3 + 1);
  smokeDetected = (status == "ALERT");
  sensorValid   = true;
  dataReceived  = true;

  // Reset LoRa timeout if in LoRa mode
  if (currentMode == MODE_LORA) {
    lastLoraDataTime = millis();
    loraWaiting = false;
  }
}

void sendArduinoCommand(const char* cmd) {
  for (int i = 0; i < 3; i++) {
    Serial2.println(cmd);
    delay(50);
  }
  Serial.print("Sent to Arduino: ");
  Serial.println(cmd);
}


// ###############################################################
//  WIFI HEALTH CHECK + MODE SWITCHING
// ###############################################################
void checkWiFiHealth() {
  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  if (nowConnected && !wifiConnected) {
    // WiFi just came back
    wifiConnected = true;
    Serial.println("[WIFI] Reconnected!");
    connectMQTT();

    if (currentMode == MODE_LORA) {
      currentMode = MODE_WIFI;
      loraCommandSent = false;
      sendArduinoCommand("LORA,OFF");
      forceRedraw = true;
      Serial.println("[MODE] Switched to WIFI");
    }
  }
  else if (!nowConnected && wifiConnected) {
    // WiFi just dropped
    wifiConnected = false;
    wifiLostTime = millis();
    Serial.println("[WIFI] Connection lost!");
  }
  else if (!nowConnected && !wifiConnected) {
    // WiFi still down — try reconnect
    Serial.println("[WIFI] Attempting reconnect...");
    WiFi.reconnect();

    // If WiFi has been down long enough, switch to LoRa
    if (currentMode == MODE_WIFI && (millis() - wifiLostTime >= WIFI_RECONNECT_TIMEOUT)) {
      currentMode = MODE_LORA;
      forceRedraw = true;
      loraWaiting = true;
      lastLoraDataTime = millis();
      Serial.println("[MODE] Switched to LORA");

      if (!loraCommandSent) {
        sendArduinoCommand("LORA,ON");
        loraCommandSent = true;
      }
    }
  }

  // Keep MQTT alive if WiFi is up
  if (wifiConnected) {
    if (!client.connected()) {
      connectMQTT();
    }
    client.loop();
  }
}


// ###############################################################
//  WIFI MODE — MQTT + BLE + DISPLAY
// ###############################################################
void loopWiFiMode() {
  // MQTT publish
  if (dataReceived && wifiConnected &&
      (millis() - lastMqttUpdate >= MQTT_UPDATE_INTERVAL_MS)) {
    publishMQTT();
    lastMqttUpdate = millis();
  }

  // BLE broadcast
  if (dataReceived && (millis() - lastBleUpdate >= BLE_UPDATE_INTERVAL_MS)) {
    updateBLE();
    lastBleUpdate = millis();
  }

  // Display — update on new data OR every 2 seconds
  if (dataReceived || forceRedraw ||
      (sensorValid && millis() - lastScreenRefresh >= SCREEN_REFRESH_MS)) {
    drawWiFiScreen();
    lastScreenRefresh = millis();
    dataReceived = false;
    forceRedraw = false;
  }
}


// ###############################################################
//  LORA MODE — DISPLAY + BLE (BLE stays on in both modes)
// ###############################################################
void loopLoRaMode() {
  // BLE still broadcasts in LoRa mode
  if (dataReceived && (millis() - lastBleUpdate >= BLE_UPDATE_INTERVAL_MS)) {
    updateBLE();
    lastBleUpdate = millis();
  }

  // Display — update on new data OR every 2 seconds
  if (dataReceived || forceRedraw ||
      (sensorValid && millis() - lastScreenRefresh >= SCREEN_REFRESH_MS)) {
    drawLoRaScreen();
    lastScreenRefresh = millis();
    dataReceived = false;
    forceRedraw = false;
  }

  // Timeout — no LoRa data
  if (!loraWaiting && (millis() - lastLoraDataTime >= LORA_TIMEOUT_MS)) {
    loraWaiting = true;
    forceRedraw = true;
  }
}


// ###############################################################
//  WIFI SETUP & MQTT
// ###############################################################
void setupWiFi() {
  wifiConnected = connWiFi(WIFI_SSID, WIFI_PASSWORD);
  if (!wifiConnected) {
    wifiLostTime = millis();
    Serial.println("[WIFI] Failed — will retry.");
  }
}

void connectMQTT() {
  if (!wifiConnected) return;
  reconnMQTT();
  mqttConnected = client.connected();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Sender node — only process raw node data from OTHER nodes
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT RX] ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(msg);

  // Only parse raw node data format
  if (msg.startsWith("N")) {
    int n1 = msg.indexOf(',');
    if (n1 > 0) {
      int sourceNode = msg.substring(1, n1).toInt();
      if (sourceNode != myNodeId) {
        parseUART(msg);
        Serial.print("[MQTT] Got data from node ");
        Serial.println(sourceNode);
      }
    }
  }
}

void publishMQTT() {
  if (!wifiConnected) return;
  reconnMQTT();
  if (!client.connected()) return;

  // Send to Ubidots (cloud dashboard)
  sendSmokeValue(rawValue);

  // Heartbeat for observability/debugging
  char nodePayload[60];
  snprintf(nodePayload, sizeof(nodePayload), "N%d,R%d,V%.2f,%s",
           myNodeId, rawValue, voltage, smokeDetected ? "ALERT" : "CLEAR");
  sendHeartbeat(String(nodePayload));
}


// ###############################################################
//  BLE
// ###############################################################
void setupBLE() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEServer* pServer = NimBLEDevice::createServer();
  (void)pServer;
  pAdvertising = NimBLEDevice::getAdvertising();
  Serial.println("[BLE] Initialized");
}

String buildBlePayload() {
  String payload = "SMK|";
  payload += String(fromNodeId >= 0 ? fromNodeId : myNodeId);
  payload += "|";

  if (!sensorValid) {
    payload += "ERR|ERR|ERROR";
    return payload;
  }

  payload += String(rawValue);
  payload += "|";
  payload += String(voltage, 2);
  payload += "|";
  payload += (smokeDetected ? "ALERT" : "CLEAR");

  return payload;
}

void updateBLE() {
  String dataPayload = buildBlePayload();

  NimBLEAdvertisementData advData;
  NimBLEAdvertisementData scanResponse;

  std::string manufacturerData;
  manufacturerData.push_back((char)MANUFACTURER_ID_LOW);
  manufacturerData.push_back((char)MANUFACTURER_ID_HIGH);
  manufacturerData += std::string(dataPayload.c_str());

  advData.setFlags(0x06);
  advData.setManufacturerData(manufacturerData);
  scanResponse.setName(BLE_DEVICE_NAME);

  pAdvertising->stop();
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanResponse);
  pAdvertising->start();

  Serial.print("[BLE] Broadcast: ");
  Serial.println(dataPayload);
}


// ###############################################################
//  DISPLAY — WIFI MODE SCREEN
// ###############################################################
void drawWiFiScreen() {
  uint16_t bgColor = TFT_BLACK;

  if (sensorValid) {
    bgColor = smokeDetected ? TFT_RED : TFT_DARKGREEN;
  }

  M5.Lcd.fillScreen(bgColor);
  M5.Lcd.setTextColor(TFT_WHITE, bgColor);

  // Top bar: mode + node info
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 4);
  M5.Lcd.print("WIFI/MQTT [TX]");
  M5.Lcd.setCursor(160, 4);
  M5.Lcd.printf("BLE:%s", pAdvertising ? "ON" : "OFF");

  // Node info
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 18);
  M5.Lcd.printf("MY:%d", myNodeId);
  M5.Lcd.setCursor(140, 18);
  if (fromNodeId >= 0) {
    M5.Lcd.printf("FROM:%d", fromNodeId);
  }

  if (!sensorValid) {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 55);
    M5.Lcd.print("Waiting for data...");
    return;
  }

  // Status
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(8, 45);
  if (smokeDetected) {
    M5.Lcd.print("!! ALERT !!");
  } else {
    M5.Lcd.print("  CLEAR   ");
  }

  // Reading
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 80);
  M5.Lcd.printf("R:%d  V:%.2fV", rawValue, voltage);

  // Bottom bar: connection status
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 120);
  M5.Lcd.printf("WiFi:%s  MQTT:%s",
    wifiConnected ? "OK" : "DOWN",
    client.connected() ? "OK" : "DOWN");
}


// ###############################################################
//  DISPLAY — LORA FALLBACK SCREEN
// ###############################################################
void drawLoRaScreen() {
  if (loraWaiting) {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 4);
    M5.Lcd.print("!! LORA FALLBACK !!");
    M5.Lcd.setCursor(180, 4);
    M5.Lcd.print("WiFi:DOWN");

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.printf("My Node: %d", myNodeId);
    M5.Lcd.setCursor(10, 70);
    M5.Lcd.print("Waiting for LoRa...");

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 120);
    M5.Lcd.printf("BLE:%s", pAdvertising ? "ON" : "OFF");
    return;
  }

  uint16_t bgColor = smokeDetected ? TFT_RED : TFT_DARKGREEN;

  M5.Lcd.fillScreen(bgColor);
  M5.Lcd.setTextColor(TFT_WHITE, bgColor);

  // Top bar: mode indicator
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 4);
  M5.Lcd.print("!! LORA FALLBACK !!");
  M5.Lcd.setCursor(180, 4);
  M5.Lcd.print("WiFi:DOWN");

  // Node info
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 18);
  M5.Lcd.printf("MY:%d", myNodeId);
  M5.Lcd.setCursor(140, 18);
  M5.Lcd.printf("FROM:%d", fromNodeId);

  // Status
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(8, 45);
  if (smokeDetected) {
    M5.Lcd.print("!! ALERT !!");
  } else {
    M5.Lcd.print("  CLEAR   ");
  }

  // Reading
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 80);
  M5.Lcd.printf("R:%d  V:%.2fV", rawValue, voltage);

  // Bottom bar
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 120);
  M5.Lcd.printf("BLE:%s  LoRa:ACTIVE", pAdvertising ? "ON" : "OFF");
}