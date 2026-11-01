// Exercises the real serial configuration service with fake NVS/clock/RNG.
#include "config_service.h"
#include <Preferences.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <mbedtls/sha256.h>

static bool paused = false, clearCloudSucceeds = true;
static unsigned cloudClears = 0;
struct Harness {
  uc::ConfigService service;
  uint32_t id = 0;
  Harness() {
    uc::ConfigHooks hooks;
    hooks.pauseOutput = [](bool value) { paused = value; };
    hooks.clearCloudAccount = []() { ++cloudClears; return clearCloudSucceeds; };
    assert(service.begin("aabbccddeeff", "test", hooks, "0A8F"));
  }
  JsonDocument call(const char *op, JsonDocument request = JsonDocument()) {
    request["v"] = 2;
    request["id"] = ++id;
    request["op"] = op;
    String bytes;
    serializeJson(request, bytes);
    JsonDocument reply;
    assert(!deserializeJson(reply, service.processSerialRequest(bytes)));
    assert(reply["id"] == id || (bytes.length() > uc::kConfigMaxMessage && reply["id"] == 0 && reply["error"]["code"] == "TOO_LARGE"));
    return reply;
  }
};

static JsonDocument edit(Harness &h) {
  auto begun = h.call("begin");
  assert(begun["ok"] == true);
  JsonDocument draft;
  draft["session"] = begun["result"]["session"];
  draft["config"] = h.service.settings();
  draft["secrets"]["wifi"].to<JsonArray>();
  draft["secrets"]["pairs"].to<JsonArray>();
  return draft;
}
static void addNetwork(JsonDocument &draft, const String &ssid, const char *op, const char *password = "") {
  auto network = draft["config"]["wifi"].as<JsonArray>().add<JsonObject>();
  network["ssid"] = ssid;
  network["configured"] = strcmp(op, "clear") != 0;
  auto action = draft["secrets"]["wifi"].as<JsonArray>().add<JsonObject>();
  action["ssid"] = ssid;
  action["op"] = op;
  if (!strcmp(op, "replace")) action["value"] = password;
}
static void commit(Harness &h, JsonDocument &draft) {
  assert(h.call("validate", draft)["ok"] == true);
  JsonDocument apply;
  apply["session"] = draft["session"];
  apply["expectedRevision"] = h.service.settings()["revision"];
  assert(h.call("apply", apply)["ok"] == true);
}
static void wifiNetworks() {
  testNvs.clear(); testBlobKeys.clear();
  Harness h;
  auto draft = edit(h);
  addNetwork(draft, "Home", "replace", "home-password");
  addNetwork(draft, "Venue", "replace", "venue-password");
  addNetwork(draft, "Guest", "clear");
  commit(h, draft);
  Harness reboot;
  assert(reboot.service.settings()["wifi"].size() == 3);
  assert(reboot.service.wifiPassword("Home") == "home-password");
  assert(reboot.service.wifiPassword("Venue") == "venue-password");
  assert(reboot.service.wifiPassword("Guest").isEmpty());
  String readable; serializeJson(reboot.call("read"), readable);
  assert(std::string(readable.c_str()).find("password") == std::string::npos);

  draft = edit(reboot);
  draft["config"]["wifi"].to<JsonArray>();
  addNetwork(draft, "Venue", "keep"); // Reorder/remove without shifting passwords.
  addNetwork(draft, "Home", "replace", "changed-password");
  commit(reboot, draft);
  Harness updated;
  assert(updated.service.settings()["wifi"].size() == 2);
  assert(updated.service.wifiPassword("Venue") == "venue-password");
  assert(updated.service.wifiPassword("Home") == "changed-password");
  assert(updated.service.wifiPassword("Guest").isEmpty());
  draft = edit(updated);
  draft["config"]["wifi"].to<JsonArray>();
  addNetwork(draft, "Venue", "keep");
  auto valid = draft;
  addNetwork(draft, "Venue", "clear");
  assert(updated.call("validate", draft)["error"]["code"] == "INVALID");
  draft.set(valid);
  draft["config"]["wifi"][0]["ssid"] = "Renamed";
  draft["secrets"]["wifi"][0]["ssid"] = "Renamed";
  assert(updated.call("validate", draft)["error"]["code"] == "INVALID");
  draft.set(valid);
  draft["secrets"]["wifi"][0]["op"] = "replace";
  draft["secrets"]["wifi"][0]["value"] = "short";
  assert(updated.call("validate", draft)["error"]["code"] == "INVALID");
  draft.set(valid);
  draft["config"]["wifi"][0]["configured"] = false;
  assert(updated.call("validate", draft)["error"]["code"] == "INVALID");
  draft.set(valid);
  draft["secrets"]["wifi"].to<JsonArray>();
  assert(updated.call("validate", draft)["error"]["code"] == "INVALID");
  draft["config"]["wifi"].to<JsonArray>();
  for (int i = 0; i < 12; ++i) {
    auto name = String(std::string("Network-") + std::to_string(i));
    addNetwork(draft, name, "replace", "many-networks-password");
  }
  commit(updated, draft);
  Harness many;
  assert(many.service.settings()["wifi"].size() == 12);
  for (int i = 0; i < 12; ++i)
    assert(many.service.wifiPassword(String(std::string("Network-") + std::to_string(i))) == "many-networks-password");
  draft = edit(many);
  draft["config"]["wifi"].to<JsonArray>();
  for (int i = 0; i < 100; ++i)
    addNetwork(draft, String(std::string("Large-") + std::to_string(i)), "replace", "many-networks-password");
  assert(many.call("validate", draft)["error"]["code"] == "TOO_LARGE");
  assert(many.service.settings()["wifi"].size() == 12);
  draft["config"]["wifi"].to<JsonArray>();
  draft["secrets"]["wifi"].to<JsonArray>();
  commit(many, draft);
  Harness empty;
  assert(empty.service.settings()["wifi"].size() == 0);
  assert(empty.service.wifiPassword("Network-0").isEmpty());
  assert(empty.service.factoryReset());
  puts("Wi-Fi: multiple networks, open networks, keep/replace/remove, reboot, validation, size limits and clear-all passed");
}

static void actionsAndMigration() {
  testNvs.clear();
  Harness h;
  auto draft = edit(h);
  JsonDocument mapping;
  mapping.set(draft["config"]["profiles"][0]["next"][1]);
  mapping["type"] = "osc_udp"; mapping["enabled"] = true; mapping["host"] = "stage.local";
  auto actions = draft["config"]["profiles"][0]["next"].to<JsonArray>();
  for (unsigned port : {8000, 9000}) { auto a = actions.add<JsonObject>(); a.set(mapping.as<JsonObjectConst>()); a["port"] = port; }
  commit(h, draft);
  Harness reboot;
  assert(reboot.service.settings()["profiles"][0]["next"][0]["port"] == 8000);
  assert(reboot.service.settings()["profiles"][0]["next"][1]["port"] == 9000);
  draft = edit(reboot);
  actions = draft["config"]["profiles"][0]["next"].as<JsonArray>();
  while (actions.size() < 8) actions.add(mapping.as<JsonObjectConst>());
  assert(reboot.call("validate", draft)["ok"] == true);
  actions.add(mapping.as<JsonObjectConst>());
  assert(reboot.call("validate", draft)["error"]["code"] == "INVALID");
  actions.remove(8);
  actions[0]["type"] = "tcp"; actions[0]["port"] = 0;
  assert(reboot.call("validate", draft)["error"]["code"] == "INVALID");
  actions[0]["port"] = 8000; actions[0]["host"] = "";
  assert(reboot.call("validate", draft)["error"]["code"] == "INVALID");

  for (const char *role : {"tx", "rx"}) for (const char *route : {"lora", "direct"}) {
    testNvs.clear();
    JsonDocument record;
    std::ifstream old("contracts/fixtures/config-v3.json");
    JsonDocument config; assert(!deserializeJson(config, old));
    config["role"] = role; config["route"] = route; config["revision"] = 12;
    config["profiles"][0]["output"] = "osc_udp"; config["profiles"][0]["host"] = "old-stage.local"; config["profiles"][0]["port"] = 9999;
    record["config"] = config; record["owner"] = "";
    record["wifi"].to<JsonObject>(); record["pairs"].to<JsonObject>();
    String payload; serializeJson(record, payload);
    uint8_t hash[32]; mbedtls_sha256((const uint8_t*)payload.c_str(), payload.length(), hash, 0);
    String digest; for (auto byte : hash) { char text[3]; snprintf(text, sizeof(text), "%02x", byte); digest += text; }
    JsonDocument envelope; envelope["record"] = record; envelope["sha256"] = digest;
    String stored; serializeJson(envelope, stored); testNvs["a"] = stored;
    Harness migrated;
    const auto &c = migrated.service.settings();
    assert(!migrated.service.status().recoveryRequired && c["schemaVersion"] == 4);
    assert(c["revision"] == 12 && c["route"].isNull());
    bool tx = !strcmp(role, "tx");
    for (const char *event : {"previous", "next"}) {
      auto list = c["profiles"][0][event];
      assert(list.size() == (tx ? 2 : 1));
      if (tx) assert(list[0]["type"] == "lora" && list[0]["enabled"] == true);
      auto local = list[tx ? 1 : 0];
      assert(local["type"] == "osc_udp" && local["host"] == "old-stage.local" && local["port"] == 9999);
      assert(local["enabled"] == (!tx || !strcmp(route, "direct")));
      assert(local["osc"] == config["profiles"][0][event]["osc"]);
    }
    assert(testNvs.size() == 1 && testNvs["a"] == stored); // Read-only migration leaves original intact.
    auto migratedDraft = edit(migrated); commit(migrated, migratedDraft);
    Harness saved;
    assert(saved.service.settings()["schemaVersion"] == 4 && saved.service.settings()["revision"] == 13);
    assert(testNvs["a"] == stored);
  }
  testNvs.clear();
  puts("Actions: persistence, independent destinations, limits, invalid endpoints, and atomic v3 TX/RX migration passed");
}

static void cloudCredentials() {
  testNvs.clear(); testBlobKeys.clear();
  Harness h;
  const String room(std::string(32, 'a')), token(std::string(64, 'b'));
  JsonDocument request; request["room"] = room; request["token"] = token;
  assert(h.call("cloud", request)["ok"] == true);
  assert(h.service.cloudRoom() == room && h.service.cloudToken() == token);
  Harness reboot;
  assert(reboot.service.cloudToken() == token);
  String readable; serializeJson(reboot.call("status"), readable);
  assert(std::string(readable.c_str()).find(token.c_str()) == std::string::npos);
  assert(reboot.call("status")["result"]["cloud"]["configured"] == true);
  auto draft = edit(reboot);
  assert(reboot.call("cloud", request)["error"]["code"] == "BUSY");
  commit(reboot, draft); // A normal settings save must preserve cloud credentials.
  Harness saved; assert(saved.service.cloudToken() == token);
  request["token"] = "invalid";
  assert(saved.call("cloud", request)["error"]["code"] == "INVALID");
  request["token"] = String(std::string(64, 'c'));
  testWriteFailure = true;
  assert(saved.call("cloud", request)["error"]["code"] == "PERSIST_FAILED");
  testWriteFailure = false;
  assert(saved.service.cloudToken() == token);
  Harness failed; assert(failed.service.cloudToken() == token);
  request["room"] = ""; request["token"] = "";
  clearCloudSucceeds = false;
  assert(failed.call("cloud", request)["error"]["code"] == "PERSIST_FAILED");
  assert(failed.service.cloudToken() == token);
  clearCloudSucceeds = true;const unsigned beforeClear = cloudClears;
  assert(failed.call("cloud", request)["ok"] == true);
  assert(cloudClears == beforeClear + 1);
  Harness cleared; assert(cleared.service.cloudToken().isEmpty());
  request["room"] = room; request["token"] = token;
  assert(cleared.call("cloud", request)["ok"] == true);
  assert(cleared.service.factoryReset());
  Harness reset; assert(reset.service.cloudRoom().isEmpty() && reset.service.cloudToken().isEmpty());
  puts("Cloud credentials: USB provisioning, redaction, reboot, settings preservation, write failure, clear and reset passed");
}

int main() {
  Harness h;
  assert(!h.service.status().setup);
  assert(h.service.settings()["profiles"][0]["name"] == "0A8F");
  assert(h.call("read")["ok"] == true); // USB needs no code or prior setup and does not pause the keyboard.
  assert(!h.service.status().setup && !h.service.outputPaused() && !paused);
  h.service.poll();
  assert(!paused && !h.service.status().setup);
  assert(testNvs.empty()); // Reading/connecting does not persist credentials.
  auto status = h.call("status");
  assert(status["result"]["capabilities"]["ble_config"] == false);
  assert(status["result"]["capabilities"]["serial_config"] == true);
  assert(h.service.settings()["profiles"][0]["next"][0]["type"] == "lora");
  for (auto op : {"code", "authorize", "claim", "recover", "receiver"})
    assert(h.call(op)["error"]["code"] == "UNSUPPORTED");

  h.service.exitSetup();
  assert(!paused && !h.service.outputPaused());
  h.service.enterSetup(); // Physical setup still authorizes output-host pairing.
  assert(h.service.status().setup);
  assert(h.service.status().passkey >= 100000 && h.service.status().passkey <= 999999);

  h.service.exitSetup();
  auto begun = h.call("begin");
  assert(begun["ok"] == true && !h.service.status().setup);
  assert(h.service.status().editing && h.service.outputPaused() && paused);
  JsonDocument draft;
  draft["session"] = begun["result"]["session"];
  draft["config"] = h.service.settings();
  draft["config"]["profiles"][0]["name"] = "Independent";
  draft["config"]["profiles"][0]["next"][0]["type"] = "cloud";
  draft["config"]["profiles"][0]["next"][0]["host"] = "api.uberclicker.com";
  draft["config"]["profiles"][0]["next"][0]["port"] = 443;
  draft["secrets"]["wifi"].to<JsonArray>();
  draft["secrets"]["pairs"].to<JsonArray>();
  draft["config"]["profiles"][0]["next"][0]["port"] = 80;
  assert(h.call("validate", draft)["error"]["code"] == "INVALID");
  draft["config"]["profiles"][0]["next"][0]["port"] = 443;
  draft["config"]["profiles"][0]["next"][0]["host"] = "other.example.com";
  assert(h.call("validate", draft)["error"]["code"] == "INVALID");
  draft["config"]["profiles"][0]["next"][0]["host"] = "api.uberclicker.com";
  assert(h.call("validate", draft)["ok"] == true);
  assert(h.service.settings()["profiles"][0]["name"] != "Independent");
  JsonDocument apply;
  apply["session"] = begun["result"]["session"];
  apply["expectedRevision"] = h.service.settings()["revision"];
  testWriteFailure = true;
  assert(h.call("apply", apply)["error"]["code"] == "PERSIST_FAILED");
  assert(h.service.settings()["profiles"][0]["name"] != "Independent");
  testWriteFailure = false;
  assert(h.call("apply", apply)["ok"] == true);
  assert(h.service.settings()["profiles"][0]["name"] == "Independent");
  assert(!h.service.outputPaused() && !paused);

  Harness reboot;
  assert(reboot.service.settings()["profiles"][0]["next"][0]["type"] == "cloud");
  assert(reboot.service.settings()["profiles"][0]["next"][0]["host"] == "api.uberclicker.com");
  assert(reboot.service.settings()["profiles"][0]["next"][0]["port"] == 443);
  assert(reboot.call("read")["result"]["config"]["profiles"][0]["name"] == "Independent");
  JsonDocument remote;
  assert(!deserializeJson(remote, reboot.service.processRemoteRequest(
      "112233445566", "{\"v\":2,\"id\":1,\"op\":\"read\"}")));
  assert(remote["ok"] == false);
  auto edit = reboot.call("begin");
  assert(edit["ok"] == true && paused);
  JsonDocument cancel;
  cancel["session"] = edit["result"]["session"];
  assert(reboot.call("cancel", cancel)["ok"] == true);
  assert(!paused && !reboot.service.outputPaused());
  assert(reboot.call("begin")["ok"] == true);
  testMillis += 120001;
  reboot.service.poll();
  assert(!reboot.service.status().setup && !reboot.service.status().editing);
  assert(!reboot.service.outputPaused());
  assert(reboot.call("read")["ok"] == true);
  assert(!reboot.service.outputPaused() && !paused); // Reads keep the keyboard active.
  assert(reboot.service.factoryReset());
  assert(reboot.service.settings()["profiles"][0]["name"] == "0A8F");
  wifiNetworks();
  actionsAndMigration();
  cloudCredentials();
  puts("Serial-only config: trusted reads, disabled BLE/code/radio config, output pairing, atomic persistence/reboot and lease expiry passed");
}
