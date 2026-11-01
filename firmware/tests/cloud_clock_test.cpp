#include "cloud_clock.h"
#include <cassert>
#include <cstdio>
int main() {
  cloud_output::Clock clock;
  assert(!clock.at(0, 0));
  assert(!clock.sync(1704067201000LL, 0, 501));
  assert(clock.sync(1704067201000LL, 100, 120));
  assert(clock.at(120, 130) == 1704067201010LL);
  assert(clock.at(130, 130) == 1704067201020LL);
  assert(!clock.at(120, 870));
  assert(clock.sync(1704067201000LL, UINT32_MAX - 10, 10));
  assert(clock.at(10, 30) == 1704067201010LL);
  assert(!clock.sync(0, 100, 100));
  puts("Cloud clock: sync RTT, stale cues and millis rollover passed");
}
