#pragma once
#include <Arduino.h>
#include "action_sequence.h"
namespace cloud_output {
bool begin();
void configure(const String &room, const String &token, bool enabled, const String &name = "M5 clicker");
void pause(bool paused);
outputs::Result submit(bool next, uint32_t issuedAt);
bool takeResult(outputs::Result &result);
bool connected();
void pairingCode(char *code, size_t size);
bool forgetAccount();
}
