#pragma once
#include <cmath>
#include <stdint.h>
namespace radio_health {
inline uint32_t spacing(uint32_t frameMs) {
  return frameMs * 20 > 10000 ? frameMs * 20 : 10000;
}
inline uint32_t replySpacing(uint32_t frameMs) {
  return spacing(frameMs) - 2 * frameMs - 1000;
}
inline uint32_t freshness(unsigned peers, uint32_t frameMs) {
  return spacing(frameMs) * (peers ? peers : 1) + 2 * frameMs + 2000;
}
struct Observation {
  bool known = false, signal = false, missed = false, probePending = false;
  float rssi = 0, snr = 0;
  uint32_t seen = 0, serial = 0, probeAt = 0, probeSerial = 0;
  void accept(float receivedRssi, float receivedSnr, uint32_t now) {
    known = true;
    missed = false;
    seen = now;
    ++serial;
    signal = std::isfinite(receivedRssi) && std::isfinite(receivedSnr) &&
             receivedRssi >= -160 && receivedRssi <= 0;
    if (signal) {
      rssi = receivedRssi;
      snr = receivedSnr;
    }
  }
  void startProbe(uint32_t now) {
    probePending = true;
    probeAt = now;
    probeSerial = serial;
  }
  bool awaitingProbe(uint32_t now, uint32_t frameMs) const {
    return probePending && uint32_t(now - probeAt) <= 2 * frameMs + 1000;
  }
  void expireProbe(uint32_t now, uint32_t frameMs) {
    if (probePending && uint32_t(now - probeAt) > 2 * frameMs + 1000) {
      probePending = false;
      if (serial == probeSerial)
        missed = true;
    }
  }
  void expire(uint32_t now, uint32_t ttl, uint32_t frameMs) {
    expireProbe(now, frameMs);
    if (known && uint32_t(now - seen) > ttl)
      missed = true;
  }
  bool online(uint32_t now, uint32_t ttl) const {
    return known && !missed && uint32_t(now - seen) <= ttl;
  }
};
} // namespace radio_health
