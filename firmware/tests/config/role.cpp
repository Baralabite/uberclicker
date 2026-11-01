#include "config_service.h"
#include "../../../contracts/config_framing.h"
#include <Preferences.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <mbedtls/sha256.h>

static String hash(const String &text) {
  uint8_t bytes[32];
  mbedtls_sha256((const uint8_t *)text.c_str(), text.length(), bytes, 0);
  String result;
  for (auto byte : bytes) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", byte);
    result += hex;
  }
  return result;
}
static String persist(JsonDocument &record, const char *slot) {
  String payload, bytes;
  serializeJson(record, payload);
  JsonDocument envelope;
  envelope["record"] = record;
  envelope["sha256"] = hash(payload);
  serializeJson(envelope, bytes);
  testNvs[slot] = bytes;
  return bytes;
}
static JsonDocument call(uc::ConfigService &service,
                         const char *op, JsonDocument request = JsonDocument()) {
  request["v"] = 2; request["id"] = 1; request["op"] = op;
  String bytes;
  serializeJson(request, bytes);
  JsonDocument reply;
  assert(!deserializeJson(reply, service.processSerialRequest(bytes)));
  assert(reply["id"] == 1);
  return reply;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const char *role = argv[1];
  const char *opposite = strcmp(role, "rx") ? "rx" : "tx";
  uc::ConfigService cold;
  assert(cold.begin("aabbccddeeff", "test"));
  assert(cold.settings()["role"] == role);
  assert(cold.settings()["profiles"][0]["name"] == "EEFF");

  JsonDocument config, record;
  std::ifstream fixture("contracts/fixtures/config-v2.json");
  assert(!deserializeJson(config, fixture));
  config["role"] = opposite;
  config["revision"] = 7;
  config["schemaVersion"] = 2;
  config["wifi"].to<JsonObject>();
  config["wifi"]["ssid"] = "Saved network";
  config["wifi"]["configured"] = true;
  String token(std::string(64, 'a'));
  record["config"] = config;
  record["owner"] = token;
  record["wifi"] = "saved-password";
  record["pairs"].to<JsonObject>();
  String saved = persist(record, "a");

  testWriteFailure = true;
  uc::ConfigService failed;
  assert(failed.begin("aabbccddeeff", "test"));
  assert(failed.status().recoveryRequired && failed.outputPaused());
  assert(testNvs["a"] == saved && testNvs.size() == 1);
  testWriteFailure = false;

  uc::ConfigService service;
  assert(service.begin("aabbccddeeff", "test"));
  assert(service.settings()["role"] == role);
  assert(service.settings()["revision"] == 8);
  auto actions = service.settings()["profiles"][0]["next"];
  if (!strcmp(role, "rx")) assert(actions.size() == 1 && actions[0]["type"] == "ble_keyboard" && actions[0]["enabled"] == true);
  else assert(actions.size() == 2 && actions[0]["type"] == "lora" && actions[0]["enabled"] == true && actions[1]["enabled"] == false);
  assert(service.settings()["wifi"][0]["ssid"] == "Saved network");
  assert(service.wifiPassword() == "saved-password");
  assert(service.status().claimed && !service.status().recoveryRequired);
  assert(testNvs["a"] == saved); // The prior complete record survives the role change.
  uc::ConfigService reboot;
  assert(reboot.begin("aabbccddeeff", "test"));
  assert(reboot.settings()["role"] == role && reboot.settings()["revision"] == 8);
  assert(reboot.status().claimed && reboot.wifiPassword() == "saved-password");
  service.enterSetup();
  auto status = call(service, "status");
  assert(status["result"]["capabilities"]["fixed_role"] == role);
  assert(status["result"]["capabilities"]["ble_config"] == false);
  assert(status["result"]["capabilities"]["serial_config"] == true);
  auto begun = call(service, "begin");
  JsonDocument draft;
  draft["session"] = begun["result"]["session"];
  draft["config"] = service.settings();
  draft["config"]["role"] = opposite;
  draft["secrets"]["wifi"][0]["ssid"] = "Saved network";
  draft["secrets"]["wifi"][0]["op"] = "keep";
  draft["secrets"]["pairs"].to<JsonArray>();
  assert(call(service, "validate", draft)["error"]["code"] == "INVALID");
  draft["config"]["role"] = role;
  assert(call(service, "validate", draft)["ok"] == true);

  if (!strcmp(role, "rx")) {
    record["config"]["role"] = "tx";
    record["config"]["revision"] = 9;
    auto peers = record["config"]["peers"].to<JsonArray>();
    for (const char *id : {"112233445566", "223344556677"}) {
      auto peer = peers.add<JsonObject>();
      peer["deviceId"] = id; peer["pairId"] = id[0] == '1' ? "1111111111111111" : "2222222222222222";
      peer["name"] = "Saved receiver"; peer["adminAuthorized"] = true;
      record["pairs"][id] = String(std::string(64, 'b'));
    }
    String latest = persist(record, "b");
    uc::ConfigService conflict;
    assert(conflict.begin("aabbccddeeff", "test"));
    assert(conflict.status().recoveryRequired);
    assert(testNvs["a"] == saved && testNvs["b"] == latest);
  }
  assert(service.factoryReset());
  assert(service.settings()["role"] == role);
  assert(service.settings()["profiles"][0]["name"] == "EEFF");
  std::cout << role << ": boot/reset role, atomic role save/reboot/failure, saved credentials, role lock and topology safety passed\n";
}
