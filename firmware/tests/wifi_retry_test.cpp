#include "wifi_retry.h"
#include <cassert>
int main() {
  outputs::WifiRetry retry;
  size_t index = 99;
  assert(!retry.attempt(0, false, 0, index));
  assert(retry.attempt(3, false, 0, index) && index == 0);
  assert(!retry.attempt(3, false, 14999, index));
  assert(retry.attempt(3, false, 15000, index) && index == 1);
  assert(!retry.attempt(3, true, 16000, index));
  assert(!retry.attempt(3, true, 90000, index));
  assert(retry.attempt(3, false, 90001, index) && index == 0);
  assert(retry.attempt(3, false, 105001, index) && index == 1);
  assert(retry.attempt(3, false, 120001, index) && index == 2);
  assert(retry.attempt(3, false, 135001, index) && index == 0);
  retry.reset();
  assert(retry.attempt(1, false, UINT32_MAX - 100, index) && index == 0);
  assert(!retry.attempt(1, false, 100, index));
  assert(retry.attempt(1, false, 15000, index) && index == 0);
  retry.reset();
  assert(retry.attempt(2, false, 15001, index) && index == 0);
}
