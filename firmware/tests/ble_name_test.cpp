#include "ble_name.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  char name[ble_name::Capacity];
  ble_name::format(name,sizeof(name),true,"John Board",0x0a8f);
  assert(!strcmp(name,"UberClicker TX: John Board"));
  assert(ble_name::prefixLength(name)==strlen(name));
  ble_name::format(name,sizeof(name),false,"John Board",0x0a8f);
  assert(!strcmp(name,"UberClicker RX: John Board"));
  ble_name::format(name,sizeof(name),false,"",0x0a8f);
  assert(!strcmp(name,"UberClicker RX: 0A8F"));
  ble_name::format(name,sizeof(name),true,"",0);
  assert(!strcmp(name,"UberClicker TX: 0000"));
  ble_name::format(name,sizeof(name),true,"123456789012345678901234",0);
  assert(strlen(name)==40);
  assert(ble_name::prefixLength(name)==29);
  assert(ble_name::prefixLength(name,31)==31);
  ble_name::format(name,sizeof(name),true,"123456789012🦘",0);
  assert(ble_name::prefixLength(name)==28); // Do not advertise half a UTF-8 code point.
  assert(ble_name::prefixLength(name,31)==28);
  assert(ble_name::prefixLength(name,32)==32);
  puts("BLE names: role/device name, zero-padded MAC fallback, length bounds and UTF-8 truncation passed");
}
