// JUST DECLARES func

#ifndef GATEWAY_H
#define GATEWAY_H

#include <WiFi.h>
#include <PubSubClient.h>

// Shared MQTT client — defined once in gateway.cpp, used everywhere
extern WiFiClient  espClient;
extern PubSubClient client;

void sendHeartbeat(const String& msg);
void sendSmokeValue(int smokeVal);
bool connWiFi(const char* ssid, const char* password);
void disconnWiFi();
void reconnMQTT();

#endif