#pragma once
#include <stdint.h>
namespace cloud_output {
struct Clock {
  bool synced = false;
  int64_t server = 0;
  uint32_t local = 0;
  bool sync(int64_t now, uint32_t started, uint32_t received) {
    uint32_t rtt = received - started;
    synced = now > 1704067200000LL && rtt <= 500;
    if (synced) { server = now + rtt / 2; local = received; }
    return synced;
  }
  int64_t at(uint32_t issued, uint32_t now) const {
    return synced && uint32_t(now - issued) < 750 ? server + uint32_t(now - local) - uint32_t(now - issued) : 0;
  }
};
}
