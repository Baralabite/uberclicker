#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace ble_name {
constexpr size_t Capacity = 41, AdvertisedBytes = 29;
inline void format(char *name, size_t capacity, bool tx, const char *deviceName,
                   uint16_t macSuffix) {
  if (deviceName[0])
    snprintf(name, capacity, "UberClicker %s: %s", tx ? "TX" : "RX", deviceName);
  else
    snprintf(name, capacity, "UberClicker %s: %04X", tx ? "TX" : "RX", unsigned(macSuffix));
}
inline size_t prefixLength(const char *name, size_t maximum = AdvertisedBytes) {
  size_t length = strlen(name);
  if (length <= maximum) return length;
  length = maximum;
  while (length && (uint8_t(name[length]) & 0xc0) == 0x80) --length;
  return length;
}
} // namespace ble_name
