/*
 * M5StickC Plus — LoRa Smoke Monitor Display
 * Receives UART strings from Arduino receiver/sender node
 * and displays Node ID, status, and last reading on screen.
 *
 * UART Wiring (M5StickC Plus ← Arduino):
 *   Grove Port PIN 32 (RX) ← Arduino Pin 4 (TX)
 *   GND                    ← Arduino GND
 *
 * Expected UART string format:
 *   "N<id>,R<raw>,V<voltage>,<ALERT|CLEAR>\n"
 *   Example: "N2,R750,V3.67,ALERT"
 *
 * Also handles "READY,<id>" startup message from Arduino.
 *
 * Library required: M5StickCPlus (install via Arduino Library Manager)
 */

#include <M5StickCPlus.h>

#define UART_BAUD   9600
#define SCREEN_W    240
#define SCREEN_H    135

// Last known values — shown on screen
int   lastNodeId  = -1;
int   lastRaw     = -1;
float lastVoltage = -1.0;
String lastStatus = "WAITING...";
bool  isAlert     = false;

String incomingBuffer = "";

// ---------------------------------------------------------------
void setup() {
  M5.begin();
  M5.Lcd.setRotation(3); // Landscape

  // Serial2 on M5StickC Plus uses Grove port: TX=26, RX=32
  Serial2.begin(UART_BAUD, SERIAL_8N1, 32, 26);

  drawWaiting();
}

// ---------------------------------------------------------------
void loop() {
  M5.update();

  // Read incoming UART bytes into buffer
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      incomingBuffer.trim();
      if (incomingBuffer.length() > 0) {
        parseAndDisplay(incomingBuffer);
      }
      incomingBuffer = "";
    } else {
      incomingBuffer += c;
    }
  }
}

// ---------------------------------------------------------------
void parseAndDisplay(String data) {
  // Handle READY message from Arduino on startup
  if (data.startsWith("READY,")) {
    int nodeId = data.substring(6).toInt();
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 50);
    M5.Lcd.printf("Node %d ready", nodeId);
    return;
  }

  // Parse: "N<id>,R<raw>,V<voltage>,<ALERT|CLEAR>"
  int n1 = data.indexOf(',');
  int n2 = data.indexOf(',', n1 + 1);
  int n3 = data.indexOf(',', n2 + 1);

  if (n1 < 0 || n2 < 0 || n3 < 0) return; // Malformed

  lastNodeId  = data.substring(1, n1).toInt();           // skip 'N'
  lastRaw     = data.substring(n1 + 2, n2).toInt();      // skip 'R'
  lastVoltage = data.substring(n2 + 2, n3).toFloat();    // skip 'V'
  lastStatus  = data.substring(n3 + 1);
  isAlert     = (lastStatus == "ALERT");

  drawStatus();
}

// ---------------------------------------------------------------
void drawStatus() {
  // Background color: red for ALERT, green for CLEAR
  uint16_t bgColor   = isAlert ? TFT_RED   : TFT_DARKGREEN;
  uint16_t textColor = TFT_WHITE;

  M5.Lcd.fillScreen(bgColor);

  // --- Node ID (top left, small) ---
  M5.Lcd.setTextColor(textColor, bgColor);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 8);
  M5.Lcd.printf("NODE %d", lastNodeId);

  // --- Status (centre, large) ---
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(8, 45);
  if (isAlert) {
    M5.Lcd.print("!! ALERT !!");
  } else {
    M5.Lcd.print("  CLEAR   ");
  }

  // --- Last reading (bottom) ---
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 100);
  M5.Lcd.printf("R:%d  V:%.2fV", lastRaw, lastVoltage);
}

// ---------------------------------------------------------------
void drawWaiting() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 55);
  M5.Lcd.print("Waiting for data...");
}
