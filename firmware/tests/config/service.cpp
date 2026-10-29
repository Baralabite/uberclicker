// Executes actual config_service.cpp; only BLE/NVS/clock/RNG adapters are fake.
#include "../../../contracts/config_framing.h"
#include "config_service.h"
#include <Preferences.h>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <mbedtls/sha256.h>
static bool paused = false;
static unsigned updateRequests = 0;
static bool acceptUpdateRequest = true;
struct Harness {
  uc::ConfigService service;
  BLEServer server;
  uint32_t id = 0;
  uc::ConfigFrames frames;
  std::function<void()> bridge;
  Harness(const char *identity = "aabbccddeeff", bool updates = false) {
    uc::ConfigHooks hooks;
    hooks.pauseOutput = [](bool p) { paused = p; };
    if (updates)
      hooks.openUpdates = []() {
        ++updateRequests;
        return acceptUpdateRequest;
      };
    assert(service.begin(&server, identity, "0.1.0", hooks));
    service.enterSetup();
    service.onSecurity(1, true, true);
    service.poll();
  }
  JsonDocument call(const char *op, JsonDocument q = JsonDocument()) {
    q["v"] = 2;
    q["id"] = ++id;
    q["op"] = op;
    String bytes;
    serializeJson(q, bytes);
    auto in = server.service->characteristics[uc::kConfigWriteUuid],
         out = server.service->characteristics[uc::kConfigNotifyUuid];
    out->outgoing.clear();
    ble_gap_conn_desc desc{1, {true, true}};
    for (size_t off = 0; off < bytes.length(); off += 12) {
      uint8_t f[20] = {0x55,
                       0x43,
                       1,
                       (uint8_t)id,
                       (uint8_t)off,
                       (uint8_t)(off >> 8),
                       (uint8_t)bytes.length(),
                       (uint8_t)(bytes.length() >> 8)};
      size_t n = std::min((size_t)12, bytes.length() - off);
      memcpy(f + 8, bytes.c_str() + off, n);
      in->setValue(f, n + 8);
      in->callback->onWrite(in, &desc);
      service.poll();
      if (bridge)
        bridge();
    }
    for (int n = 0; n < 900; n++) {
      service.poll();
      if (bridge)
        bridge();
    }
    frames.clear();
    bool complete = false;
    for (auto &f : out->outgoing)
      if (frames.feed(f.data(), f.size(), testMillis) == 1)
        complete = true;
    assert(complete);
    JsonDocument r;
    assert(!deserializeJson(r, frames.bytes, frames.used));
    return r;
  }
  void sendOnly(const char *op, JsonDocument q, uint32_t requestId) {
    q["v"] = 2;
    q["id"] = requestId;
    q["op"] = op;
    String body;
    serializeJson(q, body);
    auto input = server.service->characteristics[uc::kConfigWriteUuid];
    server.service->characteristics[uc::kConfigNotifyUuid]->outgoing.clear();
    ble_gap_conn_desc desc{1, {true, true}};
    for (size_t off = 0; off < body.length(); off += 12) {
      uint8_t f[20] = {0x55,
                       0x43,
                       1,
                       (uint8_t)requestId,
                       (uint8_t)off,
                       (uint8_t)(off >> 8),
                       (uint8_t)body.length(),
                       (uint8_t)(body.length() >> 8)};
      size_t n = std::min((size_t)12, body.length() - off);
      memcpy(f + 8, body.c_str() + off, n);
      input->setValue(f, n + 8);
      input->callback->onWrite(input, &desc);
      service.poll();
    }
  }
  String claim() {
    JsonDocument q;
    char code[7];
    snprintf(code, 7, "%06u", service.status().passkey);
    q["code"] = code;
    auto r = call("claim", std::move(q));
    assert(r["ok"] == true);
    return r["result"]["token"].as<String>();
  }
  String begin() {
    auto r = call("begin");
    assert(r["ok"] == true);
    assert(paused);
    return r["result"]["session"].as<String>();
  }
  JsonDocument stage(String session) {
    JsonDocument q;
    q["session"] = session;
    q["config"] = service.settings();
    q["secrets"]["wifi"]["op"] = "keep";
    auto ops = q["secrets"]["pairs"].to<JsonArray>();
    for (auto p : service.settings()["peers"].as<JsonArrayConst>()) {
      auto op = ops.add<JsonObject>();
      op["deviceId"] = p["deviceId"];
      op["op"] = "keep";
    }
    return q;
  }
  JsonDocument apply(String session, uint32_t revision) {
    JsonDocument q;
    q["session"] = session;
    q["expectedRevision"] = revision;
    return call("apply", std::move(q));
  }
};
static String testKey(char digit) {
  String key;
  for (int i = 0; i < 64; i++)
    key += digit;
  return key;
}
static void addPeer(JsonDocument &q, unsigned index, const char *id,
                    const char *pairId, const char *name, const String &key) {
  auto p = q["config"]["peers"][index];
  p["deviceId"] = id;
  p["pairId"] = pairId;
  p["name"] = name;
  p["adminAuthorized"] = true;
  auto secret = q["secrets"]["pairs"][index];
  secret["deviceId"] = id;
  secret["op"] = "replace";
  secret["value"] = key;
}
static void remoteTests() {
  testNvs.clear();
  Harness tx;
  String txOwner = tx.claim();
  auto token = tx.begin();
  auto q = tx.stage(token);
  addPeer(q, 0, "112233445566", "0102030405060708", "Room A", testKey('1'));
  addPeer(q, 1, "223344556677", "1112131415161718", "Room B", testKey('2'));
  assert(tx.call("validate", std::move(q))["ok"] == true);
  assert(tx.apply(token, 1)["ok"] == true);
  auto txStore = testNvs;
  assert(tx.service.pairKeyHex("112233445566") == testKey('1'));
  assert(tx.service.pairKeyHex("223344556677") == testKey('2'));
  assert(tx.service.pairKeyHex("deadbeef0000").isEmpty());
  testNvs.clear();
  Harness rx("112233445566");
  rx.claim();
  token = rx.begin();
  q = rx.stage(token);
  q["config"]["role"] = "rx";
  addPeer(q, 0, "aabbccddeeff", "0102030405060708", "Controller", testKey('1'));
  assert(rx.call("validate", std::move(q))["ok"] == true);
  assert(rx.apply(token, 1)["ok"] == true);
  rx.service.exitSetup();
  rx.service.onDisconnect(1);
  rx.service.poll();
  auto rxStore = testNvs;
  testNvs.clear();
  Harness rxB("223344556677");
  rxB.claim();
  auto otherSession = rxB.begin();
  q = rxB.stage(otherSession);
  q["config"]["role"] = "rx";
  addPeer(q, 0, "aabbccddeeff", "1112131415161718", "Controller", testKey('2'));
  assert(rxB.call("validate", std::move(q))["ok"] == true);
  assert(rxB.apply(otherSession, 1)["ok"] == true);
  rxB.service.exitSetup();
  rxB.service.onDisconnect(1);
  rxB.service.poll();
  auto rxBStore = testNvs;
  bool secondAvailable = false;
  tx.service.setRemoteTransportAvailable(true);
  tx.bridge = [&]() {
    String peer, request;
    uint32_t correlation;
    if (!tx.service.takeRemoteRequest(peer, request, correlation))
      return;
    JsonDocument failure;
    String response;
    if (peer == "112233445566" || (peer == "223344556677" && secondAvailable)) {
      bool first = peer == "112233445566";
      testNvs = first ? rxStore : rxBStore;
      response = (first ? rx.service : rxB.service)
                     .processRemoteRequest("aabbccddeeff", request);
      if (first)
        rxStore = testNvs;
      else
        rxBStore = testNvs;
      testNvs = txStore;
    } else {
      failure["v"] = 2;
      failure["id"] = correlation;
      failure["ok"] = false;
      failure["error"]["code"] = "REMOTE_UNAVAILABLE";
      serializeJson(failure, response);
    }
    tx.service.completeRemoteRequest(correlation, response);
  };
  auto remote = [&](const char *operation, JsonDocument args = JsonDocument(),
                    const char *target = "112233445566") {
    args["op"] = operation;
    if (!strcmp(operation, "apply"))
      args["transactionId"] = !strcmp(target, "112233445566")
                                  ? "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                  : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    JsonDocument envelope;
    envelope["deviceId"] = target;
    envelope["request"] = args;
    return tx.call("receiver", std::move(envelope));
  };
  auto result = remote("read");
  assert(result["result"]["deviceId"] == "112233445566");
  assert(result["result"]["response"]["result"]["config"]["role"] == "rx");
  assert(remote("read", JsonDocument(), "deadbeef0000")["error"]["code"] ==
         "REMOTE_DENIED");
  assert(remote("read", JsonDocument(),
                "223344556677")["result"]["response"]["error"]["code"] ==
         "REMOTE_UNAVAILABLE");
  assert(remote("updates")["error"]["code"] == "REMOTE_DENIED");
  result = remote("begin");
  token = result["result"]["response"]["result"]["session"].as<String>();
  assert(token.length() == 16);
  assert(rx.service.status().remoteEditing);
  assert(!rx.service.status().setup);
  assert(rx.service.outputPaused());
  JsonDocument invalid;
  invalid["v"] = 2;
  invalid["id"] = 500;
  invalid["op"] = "read";
  String body;
  serializeJson(invalid, body);
  JsonDocument rejected;
  assert(!deserializeJson(
      rejected, rx.service.processRemoteRequest("ffffffffffff", body)));
  assert(rejected["error"]["code"] == "REMOTE_DENIED");
  q = rx.stage(token);
  q["config"]["profiles"][0]["name"] = "Remote room A";
  result = remote("validate", std::move(q));
  assert(result["result"]["response"]["ok"] == true);
  q.clear();
  q["session"] = token;
  q["expectedRevision"] = 1;
  assert(remote("apply", std::move(q))["result"]["response"]["error"]["code"] ==
         "STALE_REVISION");
  q.clear();
  q["session"] = token;
  q["expectedRevision"] = 2;
  assert(remote("apply", std::move(q))["result"]["response"]["ok"] == true);
  assert(rx.service.settings()["revision"] == 3);
  assert(rx.service.settings()["profiles"][0]["name"] == "Remote room A");
  assert(!rx.service.status().editing);
  assert(!rx.service.outputPaused());
  assert(tx.service.settings()["activePeer"] == 0);
  assert(tx.service.settings()["profiles"][0]["name"] == "Presenter");
  secondAvailable = true;
  auto secondResult = remote("begin", JsonDocument(), "223344556677");
  auto secondSession =
      secondResult["result"]["response"]["result"]["session"].as<String>();
  q = rxB.stage(secondSession);
  q["config"]["profiles"][0]["name"] = "Remote room B";
  assert(remote("validate", std::move(q),
                "223344556677")["result"]["response"]["ok"] == true);
  q.clear();
  q["session"] = secondSession;
  q["expectedRevision"] = 2;
  assert(remote("apply", std::move(q),
                "223344556677")["result"]["response"]["ok"] == true);
  assert(rxB.service.settings()["revision"] == 3);
  assert(rxB.service.settings()["profiles"][0]["name"] == "Remote room B");
  assert(rx.service.settings()["profiles"][0]["name"] == "Remote room A");
  assert(tx.service.settings()["activePeer"] == 0);

  q.clear();
  q["session"] = token;
  q["expectedRevision"] = 2;
  assert(remote("apply",
                std::move(q))["result"]["response"]["result"]["duplicate"] ==
         true);
  assert(rx.service.settings()["revision"] == 3);
  token =
      remote("begin")["result"]["response"]["result"]["session"].as<String>();
  q = rx.stage(token);
  q["config"]["radio"] = "long_range";
  assert(
      remote("validate", std::move(q))["result"]["response"]["error"]["code"] ==
      "REMOTE_DENIED");
  tx.service.onDisconnect(1);
  tx.service.poll();
  String peer, session;
  assert(tx.service.takeRemoteCancellation(peer, session));
  assert(peer == "112233445566" && session == token);
  invalid.clear();
  invalid["v"] = 2;
  invalid["id"] = 501;
  invalid["op"] = "cancel";
  invalid["session"] = session;
  serializeJson(invalid, body);
  assert(!deserializeJson(
      rejected, rx.service.processRemoteRequest("aabbccddeeff", body)));
  assert(rejected["ok"] == true);
  assert(!rx.service.status().editing);
  token = "";
  invalid.clear();
  invalid["v"] = 2;
  invalid["id"] = 502;
  invalid["op"] = "begin";
  serializeJson(invalid, body);
  assert(!deserializeJson(
      rejected, rx.service.processRemoteRequest("aabbccddeeff", body)));
  assert(rejected["ok"] == true);
  testMillis += 3600001;
  rx.service.poll();
  assert(!rx.service.status().editing);
  testNvs = rxStore;
  assert(rx.service.factoryReset());
  invalid["op"] = "read";
  serializeJson(invalid, body);
  assert(!deserializeJson(
      rejected, rx.service.processRemoteRequest("aabbccddeeff", body)));
  assert(rejected["error"]["code"] == "REMOTE_DENIED");
  // Reused BLE id after reconnect must never accept an old target's delayed
  // completion.
  tx.bridge = {};
  tx.service.enterSetup();
  tx.service.onSecurity(1, true, true);
  tx.service.poll();
  q.clear();
  q["token"] = txOwner;
  assert(tx.call("authorize", std::move(q))["ok"] == true);
  q.clear();
  q["deviceId"] = "112233445566";
  q["request"]["op"] = "read";
  tx.sendOnly("receiver", std::move(q), 1);
  String oldPeer, oldRequest;
  uint32_t oldCorrelation;
  assert(tx.service.takeRemoteRequest(oldPeer, oldRequest, oldCorrelation));
  tx.service.onDisconnect(1);
  tx.service.poll();
  tx.service.onSecurity(1, true, true);
  tx.service.poll();
  q.clear();
  q["token"] = txOwner;
  assert(tx.call("authorize", std::move(q))["ok"] == true);
  q.clear();
  q["deviceId"] = "223344556677";
  q["request"]["op"] = "read";
  tx.sendOnly("receiver", std::move(q), 1);
  String newPeer, newRequest;
  uint32_t newCorrelation;
  assert(tx.service.takeRemoteRequest(newPeer, newRequest, newCorrelation));
  assert(newCorrelation != oldCorrelation);
  assert(!tx.service.remoteRequestActive(oldCorrelation));
  assert(tx.service.remoteRequestActive(newCorrelation));
  JsonDocument callback;
  callback["v"] = 2;
  callback["id"] = oldCorrelation;
  callback["ok"] = true;
  callback["result"]["marker"] = "wrong target";
  String reply;
  serializeJson(callback, reply);
  tx.service.completeRemoteRequest(oldCorrelation, reply);
  tx.service.poll();
  assert(tx.service.remoteRequestActive(newCorrelation));
  assert(tx.server.service->characteristics[uc::kConfigNotifyUuid]
             ->outgoing.empty());
  callback["id"] = newCorrelation;
  callback["result"]["marker"] = "correct target";
  serializeJson(callback, reply);
  tx.service.completeRemoteRequest(newCorrelation, reply);
  for (int n = 0; n < 900; n++)
    tx.service.poll();
  uc::ConfigFrames decoded;
  for (auto &frame :
       tx.server.service->characteristics[uc::kConfigNotifyUuid]->outgoing)
    decoded.feed(frame.data(), frame.size(), testMillis);
  JsonDocument correlated;
  assert(!deserializeJson(correlated, decoded.bytes, decoded.used));
  assert(correlated["id"] == 1);
  assert(correlated["result"]["deviceId"] == "223344556677");
  assert(correlated["result"]["response"]["result"]["marker"] ==
         "correct target");
  testNvs.clear();
  testMillis = 0;
  std::cout
      << "remote config: multi-peer keys, authenticated target authority, no "
         "Rx setup, isolated revisions, stale/duplicate Apply, offline/unknown "
         "target, cancellation/expiry and reset passed\n";
}
static void migrationTests() {
  testNvs.clear();
  testBlobKeys.clear();
  JsonDocument original, record, envelope;
  std::ifstream f("contracts/fixtures/config-default.json");
  assert(!deserializeJson(original, f));
  original["schemaVersion"] = 1;
  original["role"] = "rx";
  original["revision"] = 7;
  original.remove("peers");
  original.remove("activePeer");
  original["peer"]["deviceId"] = "112233445566";
  original["peer"]["pairId"] = "0102030405060708";
  original["wifi"]["ssid"] = "Old network";
  original["wifi"]["configured"] = true;
  record["config"] = original;
  record["owner"] = testKey('a');
  record["wifi"] = "old-password";
  record["pair"] = testKey('b');
  String payload;
  serializeJson(record, payload);
  uint8_t bytes[32];
  mbedtls_sha256((const uint8_t *)payload.c_str(), payload.length(), bytes, 0);
  String sum;
  char b[3];
  for (auto n : bytes) {
    snprintf(b, 3, "%02x", n);
    sum += b;
  }
  envelope["payload"] = payload;
  envelope["sha256"] = sum;
  String persisted;
  serializeJson(envelope, persisted);
  testNvs["b"] = persisted;
  Harness migrated;
  assert(migrated.service.settings()["schemaVersion"] == 2);
  assert(migrated.service.settings()["revision"] == 7);
  assert(migrated.service.settings()["peers"].size() == 1);
  assert(migrated.service.settings()["peers"][0]["adminAuthorized"] == false);
  JsonDocument denied;
  denied["v"] = 2;
  denied["id"] = 1;
  denied["op"] = "read";
  String json;
  serializeJson(denied, json);
  JsonDocument answer;
  assert(!deserializeJson(
      answer, migrated.service.processRemoteRequest("112233445566", json)));
  assert(answer["error"]["code"] == "REMOTE_DENIED");
  assert(migrated.service.pairKeyHex("112233445566") == testKey('b'));
  assert(migrated.service.wifiPassword() == "old-password");
  assert(testNvs["b"] == persisted);
  JsonDocument q;
  q["token"] = testKey('a');
  assert(migrated.call("authorize", std::move(q))["ok"] == true);
  auto session = migrated.begin();
  q = migrated.stage(session);
  q["config"]["profiles"][0]["name"] = "Migrated show";
  assert(migrated.call("validate", std::move(q))["ok"] == true);
  testTornWrite = true;
  assert(migrated.apply(session, 7)["error"]["code"] == "PERSIST_FAILED");
  testTornWrite = false;
  Harness reboot;
  assert(reboot.service.settings()["revision"] == 7);
  assert(reboot.service.pairKeyHex() == testKey('b'));
  assert(testNvs["b"] == persisted);
  assert(migrated.apply(session, 7)["ok"] == true);
  Harness committed;
  assert(committed.service.settings()["schemaVersion"] == 2);
  assert(committed.service.settings()["revision"] == 8);
  assert(committed.service.pairKeyHex() == testKey('b'));
  testNvs.clear();
  testMillis = 0;
  std::cout << "migration: v1 owner/Wi-Fi/pair authority retained, interrupted "
               "and complete v2 commit passed\n";
}
static JsonDocument remoteCall(uc::ConfigService &service, const char *op,
                               JsonDocument args = JsonDocument()) {
  static uint32_t request = 1000;
  args["v"] = 2;
  args["id"] = ++request;
  args["op"] = op;
  String json;
  serializeJson(args, json);
  JsonDocument response;
  assert(!deserializeJson(response,
                          service.processRemoteRequest("aabbccddeeff", json)));
  return response;
}
static void durableAndLeaseTests() {
  testNvs.clear();
  testMillis = 0;
  Harness rx("112233445566");
  rx.claim();
  auto local = rx.begin();
  auto q = rx.stage(local);
  q["config"]["role"] = "rx";
  addPeer(q, 0, "aabbccddeeff", "0102030405060708", "Controller", testKey('1'));
  assert(rx.call("validate", std::move(q))["ok"] == true);
  assert(rx.apply(local, 1)["ok"] == true);
  rx.service.exitSetup();
  rx.service.onDisconnect(1);
  rx.service.poll();
  auto began = remoteCall(rx.service, "begin");
  String session = began["result"]["session"].as<String>();
  assert(began["result"]["expiresIn"] == 120);
  // A long fragmented upload can outlast120s only while core reports fresh
  // authenticated progress.
  testMillis = 110000;
  rx.service.noteRemoteProgress("aabbccddeeff");
  testMillis = 130000;
  rx.service.poll();
  assert(rx.service.status().remoteEditing);
  // Lost Begin reply/Cancel: no more fresh progress; idle cleanup resumes
  // without receiver buttons.
  testMillis = 230001;
  rx.service.poll();
  assert(!rx.service.status().editing);
  assert(!rx.service.outputPaused());
  session = remoteCall(rx.service, "begin")["result"]["session"].as<String>();
  q = rx.stage(session);
  q["config"]["profiles"][0]["name"] = "Durable receiver";
  assert(remoteCall(rx.service, "validate", std::move(q))["ok"] == true);
  JsonDocument apply;
  apply["session"] = session;
  apply["expectedRevision"] = 2;
  apply["transactionId"] = "cccccccccccccccccccccccccccccccc";
  JsonDocument exact;
  exact.set(apply);
  auto committed = remoteCall(rx.service, "apply", std::move(apply));
  assert(committed["ok"] == true);
  assert(committed["result"]["revision"] == 3);
  auto persisted = testNvs;
  // Ignore the Apply reply, reboot the actual service, and resolve through
  // authenticated controller lookup.
  Harness reboot("112233445566");
  reboot.service.exitSetup();
  reboot.service.onDisconnect(1);
  reboot.service.poll();
  q.clear();
  q["transactionId"] = "cccccccccccccccccccccccccccccccc";
  auto resolved = remoteCall(reboot.service, "result", std::move(q));
  assert(resolved["result"]["state"] == "applied");
  assert(resolved["result"]["revision"] == 3);
  assert(reboot.service.settings()["revision"] == 3);
  JsonDocument duplicate;
  duplicate.set(exact);
  auto repeated = remoteCall(reboot.service, "apply", std::move(duplicate));
  assert(repeated["ok"] == true);
  assert(repeated["result"]["duplicate"] == true);
  assert(reboot.service.settings()["revision"] == 3);
  duplicate.set(exact);
  duplicate["expectedRevision"] = 3;
  assert(remoteCall(reboot.service, "apply",
                    std::move(duplicate))["error"]["code"] ==
         "TRANSACTION_CONFLICT");
  session =
      remoteCall(reboot.service, "begin")["result"]["session"].as<String>();
  q = reboot.stage(session);
  q["config"]["profiles"][0]["name"] = "Not committed";
  assert(remoteCall(reboot.service, "validate", std::move(q))["ok"] == true);
  apply.clear();
  apply["session"] = session;
  apply["expectedRevision"] = 3;
  apply["transactionId"] = "dddddddddddddddddddddddddddddddd";
  testTornWrite = true;
  assert(remoteCall(reboot.service, "apply",
                    std::move(apply))["error"]["code"] == "PERSIST_FAILED");
  testTornWrite = false;
  Harness interrupted("112233445566");
  interrupted.service.exitSetup();
  q.clear();
  q["transactionId"] = "dddddddddddddddddddddddddddddddd";
  assert(remoteCall(interrupted.service, "result",
                    std::move(q))["result"]["state"] == "unknown");
  assert(interrupted.service.settings()["revision"] == 3);
  assert(interrupted.service.settings()["profiles"][0]["name"] ==
         "Durable receiver");
  // The earlier committed receipt survives a torn replacement and cannot be
  // read after reset.
  q.clear();
  q["transactionId"] = "cccccccccccccccccccccccccccccccc";
  assert(remoteCall(interrupted.service, "result",
                    std::move(q))["result"]["state"] == "applied");
  // A readback failure is uncertain, not permission to replay Apply; reboot resolves the receipt.
  session = remoteCall(interrupted.service, "begin")["result"]["session"].as<String>();
  q = interrupted.stage(session);
  q["config"]["profiles"][0]["name"] = "Readback recovery";
  assert(remoteCall(interrupted.service, "validate", std::move(q))["ok"] == true);
  apply.clear();
  apply["session"] = session;
  apply["expectedRevision"] = 3;
  apply["transactionId"] = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
  testReadbackFailure = true;
  assert(remoteCall(interrupted.service, "apply", std::move(apply))["error"]["code"] == "PERSIST_FAILED");
  testReadbackFailure = false;
  assert(interrupted.service.status().recoveryRequired);
  assert(interrupted.service.outputPaused());
  Harness readbackRecovered("112233445566");
  readbackRecovered.service.exitSetup();
  q.clear(); q["transactionId"] = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
  auto recovered = remoteCall(readbackRecovered.service, "result", std::move(q));
  assert(recovered["result"]["state"] == "applied");
  assert(recovered["result"]["revision"] == 4);
  q.clear(); q["transactionId"] = "cccccccccccccccccccccccccccccccc";
  assert(remoteCall(readbackRecovered.service, "result", std::move(q))["result"]["state"] == "unknown");
  assert(interrupted.service.factoryReset());
  q.clear();
  q["transactionId"] = "cccccccccccccccccccccccccccccccc";
  assert(remoteCall(interrupted.service, "result",
                    std::move(q))["error"]["code"] == "REMOTE_DENIED");
  // A valid higher/unknown persisted schema is preserved and locks
  // configuration until physical reset.
  testNvs = persisted;
  JsonDocument env, record;
  assert(!deserializeJson(env, testNvs["b"]));
  if (env["record"].is<JsonObjectConst>())
    record.set(env["record"]);
  else
    assert(!deserializeJson(record, env["payload"].as<String>()));
  record["config"]["schemaVersion"] = 99;
  String payload;
  serializeJson(record, payload);
  uint8_t sha[32];
  mbedtls_sha256((const uint8_t *)payload.c_str(), payload.length(), sha, 0);
  String sum;
  char b[3];
  for (auto n : sha) {
    snprintf(b, 3, "%02x", n);
    sum += b;
  }
  env.remove("payload");
  env["record"] = record;
  env["sha256"] = sum;
  String unknown;
  serializeJson(env, unknown);
  testNvs["b"] = unknown;
  Harness locked("112233445566");
  assert(locked.service.status().recoveryRequired);
  assert(locked.service.outputPaused());
  assert(locked.call("read")["error"]["code"] == "RECOVERY_REQUIRED");
  q.clear();
  q["token"] = testKey('1');
  assert(locked.call("authorize", std::move(q))["error"]["code"] ==
         "RECOVERY_REQUIRED");
  assert(testNvs["b"] == unknown);
  assert(locked.service.factoryReset());
  assert(!locked.service.status().recoveryRequired);
  assert(testNvs.empty());
  testMillis = 0;
  std::cout << "review gates:120s idle/fresh progress, lost Begin/Cancel "
               "recovery, atomic durable Apply+reboot lookup/dedup/conflict, "
               "torn receipt unknown, locked schema recovery passed\n";
}
static void largeRecordTests() {
  testNvs.clear(); testBlobKeys.clear(); testMillis = 0;
  testNvsOccupied = 2048; // Reserve representative Wi-Fi/bond/journal storage outside this namespace.
  testNvsPeak = 0;
  Harness h;
  String owner = h.claim();
  auto session = h.begin();
  auto q = h.stage(session);
  JsonDocument profile; profile.set(h.service.settings()["profiles"][0]);
  auto profiles = q["config"]["profiles"].to<JsonArray>();
  String name(std::string(24, '"')), password(std::string(63, '"'));
  for (unsigned index = 0; index < 4; ++index) {
    auto p = profiles.add<JsonObject>(); p.set(profile.as<JsonObjectConst>());
    p["name"] = name; p["output"] = "udp";
    p["host"] = String(std::string(63, 'h')); p["port"] = 65535;
    for (const char *action : {"previous", "next"}) {
      p[action]["osc"] = String("/") + String(std::string(63, 'o'));
      p[action]["payload"] = String(std::string(128, '"'));
    }
  }
  for (unsigned index = 0; index < 8; ++index) {
    char id[13], pairId[17];
    snprintf(id, sizeof id, "%012x", index + 256);
    snprintf(pairId, sizeof pairId, "%016x", index + 4096);
    addPeer(q, index, id, pairId, name.c_str(), testKey('1' + index));
  }
  q["config"]["activeProfile"] = 3; q["config"]["activePeer"] = 7;
  q["config"]["wifi"]["ssid"] = String(std::string(32, '"'));
  q["config"]["wifi"]["configured"] = true;
  q["secrets"]["wifi"]["op"] = "replace";
  q["secrets"]["wifi"]["value"] = password;
  assert(measureJson(q) + 64 < uc::kConfigMaxMessage);
  assert(h.call("validate", std::move(q))["ok"] == true);
  assert(h.apply(session, 1)["ok"] == true);
  size_t largest = 0;
  for (auto &entry : testNvs) largest = std::max(largest, entry.second.length());
  assert(largest > 4000 && largest <= uc::kConfigMaxMessage);
  // Replacing full records must reclaim only the inactive slot before the next atomic write.
  for (unsigned round = 0; round < 3; ++round) {
    uint32_t revision = h.service.settings()["revision"];
    session = h.begin(); q = h.stage(session);
    q["config"]["profiles"][0]["port"] = 60000 + round;
    assert(h.call("validate", std::move(q))["ok"] == true);
    assert(h.apply(session, revision)["ok"] == true);
  }
  Harness reboot;
  q.clear(); q["token"] = owner;
  assert(reboot.call("authorize", std::move(q))["ok"] == true);
  assert(reboot.service.settings()["profiles"].size() == 4);
  assert(reboot.service.settings()["peers"].size() == 8);
  assert(reboot.service.settings()["activePeer"] == 7);
  assert(reboot.service.wifiPassword() == password);
  assert(reboot.service.pairKeyHex("000000000107") == testKey('8'));
  uint32_t revision = reboot.service.settings()["revision"];
  session = reboot.begin(); q = reboot.stage(session);
  q["config"]["profiles"][0]["port"] = 50000;
  assert(reboot.call("validate", std::move(q))["ok"] == true);
  testTornWrite = true;
  assert(reboot.apply(session, revision)["error"]["code"] == "PERSIST_FAILED");
  testTornWrite = false;
  Harness interrupted;
  assert(!interrupted.service.status().recoveryRequired);
  assert(interrupted.service.settings()["revision"] == revision);
  assert(interrupted.service.settings()["profiles"][0]["port"] == 60002);
  assert(testNvsPeak <= 4 * 126 * 32);
  std::cout << "NVS maximum config: " << largest << " byte blob, " << testNvsPeak
            << " modeled entries/bytes including 2KB driver reserve; repeated saves, reboot and torn replacement passed\n";
  testNvsOccupied = 0;
  testNvs.clear();
  testBlobKeys.clear();
}
static void updateSessionTests() {
  testNvs.clear();
  testBlobKeys.clear();
  updateRequests = 0;
  Harness h("aabbccddeeff", true);
  assert(h.call("status")["result"]["capabilities"]["ota_session"] == true);
  assert(h.call("updates")["error"]["code"] == "UNAUTHORIZED");
  h.claim();
  auto edit = h.begin();
  assert(h.call("updates")["error"]["code"] == "BUSY");
  JsonDocument cancel;
  cancel["session"] = edit;
  assert(h.call("cancel", std::move(cancel))["ok"] == true);
  JsonDocument injected;
  injected["url"] = "https://example.invalid/firmware.bin";
  assert(h.call("updates", std::move(injected))["error"]["code"] == "INVALID");
  assert(updateRequests == 0);
  acceptUpdateRequest = false;
  assert(h.call("updates")["error"]["code"] == "BUSY");
  acceptUpdateRequest = true;
  assert(h.call("updates")["result"]["requested"] == true);
  assert(updateRequests == 2);
  h.service.exitSetup();
  assert(h.call("updates")["error"]["code"] == "SETUP_REQUIRED");
  assert(updateRequests == 2);
  testNvs.clear();
  testBlobKeys.clear();
  Harness unsupported;
  unsupported.claim();
  assert(unsupported.call("status")["result"]["capabilities"]["ota_session"] == false);
  assert(unsupported.call("updates")["error"]["code"] == "UNSUPPORTED");
  assert(updateRequests == 2);
  testNvs.clear();
  testBlobKeys.clear();
}
int main() {
  remoteTests();
  migrationTests();
  durableAndLeaseTests();
  largeRecordTests();
  updateSessionTests();
  std::ifstream wireFile("contracts/fixtures/config-conformance.json");
  JsonDocument wire;
  assert(!deserializeJson(wire, wireFile));
  String encoded = wire["frameHex"].as<String>();
  std::vector<uint8_t> golden;
  for (size_t i = 0; i < encoded.length(); i += 2) {
    char byte[] = {encoded[i], encoded[i + 1], 0};
    golden.push_back(strtoul(byte, nullptr, 16));
  }
  uc::ConfigFrames wireFrames;
  assert(wireFrames.feed(golden.data(), golden.size(), 0) == 1);
  JsonDocument request;
  assert(!deserializeJson(request, wireFrames.bytes, wireFrames.used));
  assert(request.as<JsonVariantConst>() ==
         wire["request"].as<JsonVariantConst>());
  Harness h;
  std::ifstream defaultFile("contracts/fixtures/config-default.json");
  JsonDocument defaultConfig;
  assert(!deserializeJson(defaultConfig, defaultFile));
  assert(h.service.settings().as<JsonVariantConst>() ==
         defaultConfig.as<JsonVariantConst>());
  assert(h.call("read")["error"]["code"] == "UNAUTHORIZED");
  // A Just Works encrypted HID bond never enters the config write queue.
  h.service.onSecurity(1, true, false);
  h.service.poll();
  auto unsafeWrite = h.server.service->characteristics[uc::kConfigWriteUuid];
  auto responses = h.server.service->characteristics[uc::kConfigNotifyUuid];
  responses->outgoing.clear();
  uint8_t raw[] = {0x55, 0x43, 1, 1, 0, 0, 2, 0, '{', '}'};
  unsafeWrite->setValue(raw, sizeof raw);
  ble_gap_conn_desc unauthenticated{1, {true, false}};
  unsafeWrite->callback->onWrite(unsafeWrite, &unauthenticated);
  h.service.poll();
  assert(responses->outgoing.empty());
  h.service.onSecurity(1, true, true);
  h.service.poll();
  auto token = h.claim();
  assert(token.length() == 64);
  assert(h.service.settings()["revision"] == 1);
  auto read = h.call("read");
  String readable;
  serializeJson(read, readable);
  assert(!strstr(readable.c_str(), token.c_str()));
  assert(!strstr(readable.c_str(), "password"));
  auto session = h.begin();
  assert(h.call("begin")["error"]["code"] == "BUSY");
  auto q = h.stage(session);
  q["config"]["profiles"][0]["name"] = "My show";
  assert(h.call("validate", std::move(q))["ok"] == true);
  assert(h.service.settings()["profiles"][0]["name"] == "Presenter");
  assert(h.apply(session, 0)["error"]["code"] == "STALE_REVISION");
  assert(h.apply(session, 1)["ok"] == true);
  assert(h.service.settings()["revision"] == 2);
  assert(h.service.settings()["profiles"][0]["name"] == "My show");
  session = h.begin();
  q = h.stage(session);
  q["config"]["wifi"]["ssid"] = "Stage";
  q["config"]["wifi"]["configured"] = true;
  q["secrets"]["wifi"]["op"] = "replace";
  q["secrets"]["wifi"]["value"] = "private-password";
  assert(h.call("validate", std::move(q))["ok"] == true);
  testWriteFailure = true;
  assert(h.apply(session, 2)["error"]["code"] == "PERSIST_FAILED");
  assert(h.service.settings()["revision"] == 2);
  testWriteFailure = false;
  assert(h.apply(session, 2)["ok"] == true);
  assert(h.service.wifiPassword() == "private-password");
  read = h.call("read");
  serializeJson(read, readable);
  assert(!strstr(readable.c_str(), "private-password"));
  session = h.begin();
  q = h.stage(session);
  q["config"]["profiles"][0]["name"] = "Torn change";
  assert(h.call("validate", std::move(q))["ok"] == true);
  testTornWrite = true;
  assert(h.apply(session, 3)["error"]["code"] == "PERSIST_FAILED");
  testTornWrite = false;
  Harness reboot;
  assert(reboot.service.settings()["revision"] == 3);
  assert(reboot.service.settings()["profiles"][0]["name"] == "My show");
  assert(reboot.service.wifiPassword() == "private-password");
  q.clear();
  q["session"] = session;
  assert(h.call("cancel", std::move(q))["ok"] == true);
  session = h.begin();
  q = h.stage(session);
  q["config"]["peers"][0]["deviceId"] = "112233445566";
  q["config"]["peers"][0]["pairId"] = "1234567890abcdef";
  q["config"]["peers"][0]["name"] = "Receiver A";
  q["config"]["peers"][0]["adminAuthorized"] = true;
  q["secrets"]["pairs"][0]["deviceId"] = "112233445566";
  q["secrets"]["pairs"][0]["op"] = "replace";
  q["secrets"]["pairs"][0]["value"] =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  assert(h.call("validate", std::move(q))["ok"] == true);
  assert(h.apply(session, 3)["ok"] == true);
  assert(h.service.pairKeyHex().length() == 64);
  read = h.call("read");
  serializeJson(read, readable);
  assert(!strstr(readable.c_str(), h.service.pairKeyHex().c_str()));
  h.service.onDisconnect(1);
  h.service.poll();
  assert(!h.service.status().editing);
  h.service.onSecurity(1, true, true);
  h.service.poll();
  assert(h.call("read")["error"]["code"] == "UNAUTHORIZED");
  q.clear();
  q["token"] = token;
  assert(h.call("authorize", std::move(q))["ok"] == true);
  session = h.begin();
  testMillis += 120001;
  h.service.poll();
  assert(!h.service.status().editing);
  assert(!h.service.status().setup);
  assert(h.call("read")["error"]["code"] == "SETUP_REQUIRED");
  h.service.enterSetup();
  q.clear();
  q["token"] = token;
  assert(h.call("authorize", std::move(q))["ok"] == true);
  session = h.begin();
  q = h.stage(session);
  q["config"]["activeProfile"] = 4;
  assert(h.call("validate", std::move(q))["error"]["code"] == "INVALID");
  q = h.stage(session);
  q["config"]["unexpected"] = true;
  assert(h.call("validate", std::move(q))["error"]["code"] == "INVALID");
  q = h.stage(session);
  q["config"]["role"] = "tx||rx";
  assert(h.call("validate", std::move(q))["error"]["code"] == "INVALID");
  q.clear();
  q["session"] = session;
  assert(h.call("cancel", std::move(q))["ok"] == true);
  testWriteFailure = true;
  assert(!h.service.factoryReset());
  testWriteFailure = false;
  assert(h.service.factoryReset());
  assert(h.service.settings()["revision"] == 0);
  assert(h.service.wifiPassword().isEmpty());
  assert(!h.service.status().claimed);
  assert(testNvs.empty());
  // A revoked token cannot reclaim; five failures close the physical window.
  auto resetCode = h.service.status().passkey;
  h.service.onSecurity(1, true, true);
  h.service.poll();
  h.claim();
  q.clear();
  q["token"] = token;
  assert(h.call("authorize", std::move(q))["error"]["code"] == "UNAUTHORIZED");
  assert(h.call("revoke")["ok"] == true);
  assert(!h.service.status().claimed);
  assert(!h.service.status().setup);
  h.service.enterSetup();
  assert(h.service.status().passkey != resetCode);
  for (int i = 0; i < 5; i++) {
    q.clear();
    q["code"] = "000000";
    auto result = h.call("claim", std::move(q));
    assert(result["error"]["code"] ==
           (i == 4 ? "RATE_LIMITED" : "UNAUTHORIZED"));
  }
  assert(!h.service.status().setup);
  assert(!paused);
  std::cout << "config service: auth, redaction, revision, atomic persistence, "
               "torn write, disconnect, expiry, validation, reset passed\n";
}
