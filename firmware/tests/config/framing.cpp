#include "../../../contracts/config_framing.h"
#include <cassert>
#include <cstring>
int main() {
  uint8_t invalid[] = {0xc0, 0x80};
  assert(!uc::configUtf8(invalid, 2));
  uint8_t surrogate[] = {0xed, 0xa0, 0x80};
  assert(!uc::configUtf8(surrogate, 3));
  assert(uc::configUtf8((const uint8_t *)"café", 5));
  uc::ConfigFrames r;
  uint8_t f[] = {0x55, 0x43, 1, 7, 0, 0, 3, 0, 'a', 'b'};
  assert(r.feed(f, sizeof f, 0) == 0);
  uint8_t g[] = {0x55, 0x43, 1, 7, 2, 0, 3, 0, 'c'};
  assert(r.feed(g, sizeof g, 1) == 1);
  assert(r.used == 3 && !memcmp(r.bytes, "abc", 3));
  r.clear();
  g[4] = 1;
  assert(r.feed(g, sizeof g, 2) == -1);
  f[6] = 1;
  f[7] = 32;
  assert(r.feed(f, sizeof f, 3) == -2);
  f[6] = 3;
  f[7] = 0;
  assert(r.feed(f, sizeof f, 4) == 0);
  assert(r.feed(f, sizeof f, 5) == -1);
  assert(r.used == 0);
  f[0] = 0;
  assert(r.feed(f, sizeof f, 6) == -1);
}
