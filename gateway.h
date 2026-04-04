#ifndef GATEWAY_H
#define GATEWAY_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// Shared TLS MQTT client.
extern WiFiClientSecure espClient;
extern PubSubClient client;

bool connWiFi(const char* ssid, const char* password);
void disconnWiFi();
void reconnMQTT();
void initMQTTSecurity();
void sendHeartbeat(const String& msg);
void sendSmokeValue(int smokeVal);
extern void mqttCallback(char* topic, byte* payload, unsigned int length);
void sendNodeData(const char* nodePayload);
#endif
