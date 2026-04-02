// JUST DECLARES func

#ifndef GATEWAY_H
#define GATEWAY_H

// [SECURITY ADDED] Use secure WiFi client for MQTT over TLS
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// Shared MQTT client — defined once in gateway.cpp, used everywhere
// [SECURITY ADDED] Secure client instead of plain WiFiClient
extern WiFiClientSecure espClient;
extern PubSubClient client;

void sendHeartbeat(const String& msg);
void sendSmokeValue(int smokeVal);
bool connWiFi(const char* ssid, const char* password);
void disconnWiFi();
void reconnMQTT();

// [SECURITY ADDED] Initialize TLS settings for secure MQTT
void initMQTTSecurity();

#endif