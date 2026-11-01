#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEServer.h>
#include <vector>
#include "action_sequence.h"
namespace outputs {
BLEServer *begin(const char *name);
bool configureName(const char *name);
void poll();
void pause(bool paused);
void configureEndpoints(JsonObjectConst profile);
struct WifiNetwork { char ssid[33], password[64]; };
void configureWifi(const std::vector<WifiNetwork> &networks);
Result submit(JsonObjectConst profile, bool next, bool (*lora)(bool) = nullptr, bool (*radioReady)() = nullptr);
bool takeResult(Result &result);
bool busy();
bool eventActive();
void securityPasskey(uint32_t key);
void clearBonds();
bool bondsCleared();
void setupWindow(bool open);
bool forgetWifi();
}
