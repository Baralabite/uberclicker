#pragma once
#include <stddef.h>
#include <stdint.h>

namespace outputs {
// Attempts are timed without blocking cue delivery or configuration changes.
class WifiRetry {
  size_t next_ = 0;
  uint32_t started_ = 0;
  bool waiting_ = false, connected_ = false;
public:
  void reset() { next_ = 0; waiting_ = connected_ = false; }
  bool attempt(size_t count, bool connected, uint32_t now, size_t &index) {
    if (!count) return false;
    if (connected) { connected_ = true; return false; }
    if (connected_) { reset(); }
    if (waiting_ && uint32_t(now - started_) < 15000) return false;
    index = next_ % count;
    next_ = (index + 1) % count;
    started_ = now;
    waiting_ = true;
    return true;
  }
};
}
