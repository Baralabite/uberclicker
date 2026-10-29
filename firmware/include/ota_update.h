#pragma once
#include <stdint.h>

namespace ota_update {
enum class State : uint8_t { Off, Idle, Checking, Available, Current, Installing, Restarting, Failed };
struct Status {
  State state = State::Off;
  char version[32] = {};
  char detail[33] = {};
};
// Only the core task calls these. The worker never owns UI or output lifecycles.
void open();
void close();
void check();
void install();
void poll();
const Status &status();
bool busy();
bool enabled();
// Defer Arduino's early confirmation until board/config/radio initialization.
void confirmBoot(bool healthy);
} // namespace ota_update
