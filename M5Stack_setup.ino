#include <M5StickCPlus.h>
#include <PubSubClient.h>

#include "gateway.h"

HardwareSerial mySerial(1);  // UART1

const char* ssid        = "iPhone";
const char* password    = "MIJAWIFI78";
const char* mqtt_server = "industrial.api.ubidots.com";

bool wifiConnected = false;
bool lastWiFiState = false;

// Standalone test heartbeat — fires every 10 s while Arduino/LoRa is absent
static unsigned long lastHeartbeat  = 0;
const  long          testInterval   = 10000;

// ── LCD helper ────────────────────────────────────────────────────────────────
void updateDisplay(const String& status, const String& lastMsg) {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(4, 4);
  M5.Lcd.println("M5Stack C+");

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(4, 28);
  M5.Lcd.println(wifiConnected      ? "WiFi : OK"   : "WiFi : FAIL");
  M5.Lcd.setCursor(4, 40);
  M5.Lcd.println(client.connected() ? "MQTT : OK"   : "MQTT : --");

  M5.Lcd.setCursor(4, 58);
  M5.Lcd.println(status);

  if (lastMsg.length()) {
    M5.Lcd.setCursor(4, 72);
    M5.Lcd.println(lastMsg.substring(0, 30));  // clamp to screen width
  }
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);   // landscape — 240 x 135
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(4, 4);
  M5.Lcd.println("Connecting...");

  Serial.begin(115200);
  mySerial.begin(9600, SERIAL_8N1, 32, 33);
  Serial.println("ESP32 Ready");

  wifiConnected = connWiFi(ssid, password);
  lastWiFiState = wifiConnected;
  client.setServer(mqtt_server, 8883); // [SECURITY ADDED] Secure MQTT over TLS port

  updateDisplay("System Ready", "");
  Serial.println("System Ready");
}

// ── loop ──────────────────────────────────────────────────────────────────────
void loop() {
  M5.update();

  bool currentWiFiState = (WiFi.status() == WL_CONNECTED);

  // WiFi watchdog — reconnect if dropped
  if (currentWiFiState != lastWiFiState) {
    if (!currentWiFiState) { 
      // WiFi LOST
      wifiConnected = false;
      updateDisplay("WiFi lost...", "");
      mySerial.println("WIFI_DOWN");   // TELL Arduino ONCE
      Serial.println("Sent WIFI_DOWN to Arduino");
    } 
    else {
      // WiFi BACK
      wifiConnected = true;
      updateDisplay("WiFi back", "");
      mySerial.println("WIFI_UP");     // TELL Arduino ONCE
      Serial.println("Sent WIFI_UP to Arduino");
    }

    lastWiFiState = currentWiFiState;
  }

  // If Arduino SENDS SOMETHING
  if (mySerial.available()) {
    String msg = mySerial.readStringUntil('\n');
    msg.trim();
    Serial.print("From Arduino: ");
    Serial.println(msg);

    if (wifiConnected) {
      reconnMQTT();
      if (msg.startsWith("C|SMOKE:")) {
        int smokeVal = msg.substring(8).toInt();  // "C|SMOKE:512" → 512
        sendSmokeValue(smokeVal);
        updateDisplay("Smoke sent", msg);
      } else {
        sendHeartbeat(msg);
        updateDisplay("UART sent", msg);
      }
    } else {
      Serial.println("WiFi down -> back to LoRa");
      mySerial.println("WIFI_DOWN");
      updateDisplay("WiFi down", msg);
    }
  }

  // If USB SENDS SOMETHING
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    mySerial.println(msg);
  }



  // ── Periodic test heartbeat (no Arduino attached yet) ──
  unsigned long now = millis();
  if (wifiConnected && (now - lastHeartbeat >= testInterval)) {
    lastHeartbeat = now;
    reconnMQTT();
    sendHeartbeat("test");
    updateDisplay("Heartbeat sent", "test");
  }

  client.loop();
}
