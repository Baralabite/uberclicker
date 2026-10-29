#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEServer.h>
namespace outputs {
enum class Result : uint8_t { Rejected, Pending, Submitted, Sent, Unknown };
BLEServer *begin(const char *name);
void poll();
void pause(bool paused);
void configureEndpoint(const char *host);
void configureWifi(const char *ssid, const char *password);
Result submit(JsonObjectConst profile, bool next);
bool takeResult(Result &result);
bool busy();
void securityPasskey(uint32_t key);
void clearBonds();
bool bondsCleared();
void setupWindow(bool open);
bool forgetWifi();
}
