#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
struct ble_gap_conn_desc {
  uint16_t conn_handle;
  struct {
    bool encrypted, authenticated;
  } sec_state;
};
class BLECharacteristic;
class BLECharacteristicCallbacks {
public:
  virtual ~BLECharacteristicCallbacks() = default;
  virtual void onWrite(BLECharacteristic *) {};
  virtual void onWrite(BLECharacteristic *c, ble_gap_conn_desc *) {
    onWrite(c);
  }
};
class BLECharacteristic {
public:
  static constexpr unsigned PROPERTY_WRITE = 1, PROPERTY_WRITE_ENC = 2,
                            PROPERTY_WRITE_AUTHEN = 4, PROPERTY_NOTIFY = 8;
  String value;
  BLECharacteristicCallbacks *callback = nullptr;
  std::vector<std::vector<uint8_t>> outgoing;
  String getValue() { return value; }
  void setCallbacks(BLECharacteristicCallbacks *c) { callback = c; }
  void setValue(uint8_t *p, size_t n) {
    value = String(std::string((char *)p, n));
  }
  void notify() {
    outgoing.emplace_back((uint8_t *)value.c_str(),
                          (uint8_t *)value.c_str() + value.length());
  }
};
class BLEService {
public:
  std::map<std::string, BLECharacteristic *> characteristics;
  BLECharacteristic *createCharacteristic(const char *s, unsigned) {
    return characteristics[s] = new BLECharacteristic;
  }
  void start() {}
};
class BLEServer {
public:
  BLEService *service = nullptr;
  BLEService *createService(const char *) { return service = new BLEService; }
};
