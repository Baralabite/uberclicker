#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace uc {
inline bool configUtf8(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n;) {
    uint8_t c = p[i++];
    if (c == 0)
      return false;
    if (c < 0x80)
      continue;
    unsigned more;
    uint32_t cp, minimum;
    if (c >= 0xc2 && c <= 0xdf) {
      more = 1;
      cp = c & 31;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      more = 2;
      cp = c & 15;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      more = 3;
      cp = c & 7;
      minimum = 0x10000;
    } else
      return false;
    if (i + more > n)
      return false;
    while (more--) {
      c = p[i++];
      if ((c & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (c & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return false;
  }
  return true;
}

// Allocation-free reassembler shared by firmware and host conformance tests.
struct ConfigFrames {
  uint8_t bytes[8193]{};
  size_t used = 0, total = 0;
  uint8_t stream = 0;
  uint32_t started = 0, touched = 0;
  void clear() { used = total = 0; }
  // 1 complete, 0 partial, -1 invalid, -2 oversized. Caller enforces timeout.
  int feed(const uint8_t *p, size_t n, uint32_t now) {
    if (used && (uint32_t(now - touched) >= 10000 ||
                 uint32_t(now - started) >= 60000)) {
      clear();
      return -1;
    }
    if (n <= 8 || n > 512 || p[0] != 0x55 || p[1] != 0x43 || p[2] != 1) {
      clear();
      return -1;
    }
    size_t off = p[4] | (p[5] << 8), len = p[6] | (p[7] << 8);
    if (len > 8192) {
      clear();
      return -2;
    }
    if (!len || off != used || (used && (stream != p[3] || total != len)) ||
        off + n - 8 > len) {
      clear();
      return -1;
    }
    if (!used) {
      total = len;
      stream = p[3];
      started = now;
    }
    memcpy(bytes + used, p + 8, n - 8);
    used += n - 8;
    touched = now;
    bytes[used] = 0;
    return used == total ? 1 : 0;
  }
};
} // namespace uc
