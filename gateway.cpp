#include "gateway.h"
#include <Arduino.h>

// Single definition of shared clients — declared extern in gateway.h
WiFiClient  espClient;
PubSubClient client(espClient);

#define TOKEN        "BBUS-Vzhx7378QAwzAIw3wyaYi5UAS5rN5h"
#define DEVICE_LABEL "m5stackcplus"
#define VARIABLE_LABEL "heartbeat"

static unsigned long lastReconnAttempt = 0;
const  long          reconnInterval    = 5000;

static char payload[150];
static char topic_pub[100];

// Publish the raw UART message as the heartbeat value (1 = alive)
// and also forward the original message string as a separate field
void sendHeartbeat(const String& msg) {
  sprintf(topic_pub, "/v1.6/devices/%s", DEVICE_LABEL);
  sprintf(payload, "{\"%s\": 1, \"raw\": \"%s\"}", VARIABLE_LABEL, msg.c_str());
  if (client.publish(topic_pub, payload)) {
    Serial.println("Published to MQTT: " + msg);
  } else {
    Serial.println("MQTT publish failed");
  }

}

// Publish a parsed smoke sensor reading to Ubidots variable "smokesensor"
void sendSmokeValue(int smokeVal) {
  sprintf(topic_pub, "/v1.6/devices/%s", DEVICE_LABEL);
  sprintf(payload, "{\"smokesensor\": %d}", smokeVal);
  if (client.publish(topic_pub, payload)) {
    Serial.printf("Published smokesensor: %d\n", smokeVal);
  } else {
    Serial.println("MQTT publish failed (smokesensor)");
  }
}

bool connWiFi(const char* ssid, const char* password) {
  uint8_t count = 0;

  WiFi.begin(ssid, password);
  Serial.printf("Connecting to AP ssid \"%s\"", ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    count++;
    Serial.print(".");

    if (count >= 10) {
      Serial.println("\nConnection failed after 10s");
      return false;
    }
  }

  Serial.printf("\nConnected to \"%s\", IP: ", ssid);
  Serial.println(WiFi.localIP());
  return true;
}

void disconnWiFi() {
  Serial.println("Disconnecting from WiFi...");

  WiFi.disconnect();
}

void reconnMQTT() {
  if (client.connected()) return;

  unsigned long now = millis();
  if (now - lastReconnAttempt < reconnInterval) return;  // rate-limit attempts
  lastReconnAttempt = now;
  Serial.println("Attempting MQTT connection...");

  if (client.connect("m5stackcplus01", TOKEN, "")) {
    Serial.println("Connected to Ubidots");
  } else {
    Serial.printf("MQTT connect failed, rc=%d — will retry in %lus\n",
                  client.state(), reconnInterval / 1000);
  }
}