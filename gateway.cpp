#include "gateway.h"
#include <Arduino.h>

WiFiClientSecure espClient;
PubSubClient client(espClient);

#define TOKEN          "BBUS-UTPLbozTlBDMM96aVmB4iyhw7NBLVI"
#define DEVICE_LABEL   "m5stick_node1"
#define HEARTBEAT_KEY  "heartbeat_1"

static unsigned long lastReconnAttempt = 0;
const  long          reconnInterval    = 5000;

static char payload[180];
static char topic_pub[100];

void initMQTTSecurity() {
  // Prototype mode TLS (encryption on, cert verification relaxed).
  espClient.setInsecure();
  Serial.println("[TLS] MQTT TLS enabled (prototype mode)");
}

void sendHeartbeat(const String& msg) {
  sprintf(topic_pub, "/v1.6/devices/%s", DEVICE_LABEL);
  sprintf(payload, "{\"%s\": 1, \"raw\": \"%s\"}", HEARTBEAT_KEY, msg.c_str());
  if (client.publish(topic_pub, payload)) {
    Serial.println("[MQTT] Heartbeat published: " + msg);
  } else {
    Serial.println("[MQTT] Heartbeat publish failed");
  }
}


// COMMENT THIS OUT IF YOUR NODE DOES NAUGHT HAVE SMOKE SENSOR
void sendSmokeValue(int smokeVal) {
  sprintf(topic_pub, "/v1.6/devices/%s", DEVICE_LABEL);
  sprintf(payload, "{\"smokesensor_4\": %d}", smokeVal);
  if (client.publish(topic_pub, payload)) {
    Serial.printf("[MQTT] smokesensor published: %d\n", smokeVal);
  } else {
    Serial.println("[MQTT] smokesensor publish failed");
  }
}

void sendNodeData(const char* nodePayload) {
  sprintf(topic_pub, "/v1.6/devices/%s/nodedata", DEVICE_LABEL);
  if (client.publish(topic_pub, nodePayload)) {
    Serial.print("[MQTT] Node data published: ");
    Serial.println(nodePayload);
  } else {
    Serial.println("[MQTT] Node data publish failed");
  }
}

bool connWiFi(const char* ssid, const char* password) {
  uint8_t count = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("[WIFI] Connecting to \"%s\"", ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    count++;
    Serial.print(".");

    if (count >= 10) {
      Serial.println("\n[WIFI] Connection failed after 10s");
      return false;
    }
  }

  Serial.printf("\n[WIFI] Connected to \"%s\", IP: ", ssid);
  Serial.println(WiFi.localIP());
  initMQTTSecurity();
  return true;
}

void disconnWiFi() {
  Serial.println("[WIFI] Disconnecting...");
  WiFi.disconnect();
}

void reconnMQTT() {
  if (client.connected()) return;

  unsigned long now = millis();
  if (now - lastReconnAttempt < reconnInterval) return;
  lastReconnAttempt = now;

  client.setServer("industrial.api.ubidots.com", 8883);
  client.setCallback(mqttCallback);
  Serial.println("[MQTT] Attempting TLS connection...");

  if (client.connect("m5node01", TOKEN, "")) {
    Serial.println("[MQTT] Connected to Ubidots over TLS");
    // Sender does NOT subscribe — it only publishes
  } else {
    Serial.printf("[MQTT] TLS connect failed, rc=%d\n", client.state());
  }
}
