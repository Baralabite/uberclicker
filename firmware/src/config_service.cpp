#include "config_service.h"
#include "../../contracts/config_framing.h"
#include <BLEDevice.h>
#include <Preferences.h>
#include <atomic>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mbedtls/sha256.h>
#include <memory>

namespace uc {
ConfigService configService;
namespace {
String randomHex(size_t n) {
  String s;
  const char *h = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    uint8_t b = esp_random();
    s += h[b >> 4];
    s += h[b & 15];
  }
  return s;
}
bool hex(const String &s, size_t n) {
  if (s.length() != n)
    return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}
bool equalSecret(const String &a, const String &b) {
  uint32_t d = a.length() ^ b.length();
  for (size_t i = 0; i < a.length(); i++)
    d |= a[i] ^ (i < b.length() ? b[i] : 0);
  return d == 0;
}
String digest(const String &s) {
  uint8_t out[32];
  mbedtls_sha256((const uint8_t *)s.c_str(), s.length(), out, 0);
  String r;
  char b[3];
  for (auto v : out) {
    snprintf(b, 3, "%02x", v);
    r += b;
  }
  return r;
}
bool keys(JsonVariantConst v, const char *allowed) {
  if (!v.is<JsonObjectConst>())
    return false;
  for (JsonPairConst p : v.as<JsonObjectConst>()) {
    if (strchr(p.key().c_str(), '|') ||
        strlen(p.key().c_str()) != p.key().size())
      return false;
    String k = "|" + String(p.key().c_str()) + "|";
    if (!strstr(allowed, k.c_str()))
      return false;
  }
  return true;
}
bool integer(JsonVariantConst v, int lo, int hi) {
  return v.is<int>() && v.as<int>() >= lo && v.as<int>() <= hi;
}
bool oneOf(const String &s, const char *v) {
  if (s.isEmpty())
    return false;
  for (char c : s)
    if (c == '|')
      return false;
  String k = "|" + s + "|";
  return strstr(v, k.c_str());
}
bool text(JsonVariantConst v, size_t lo, size_t hi) {
  if (!v.is<const char *>())
    return false;
  auto value = v.as<JsonString>();
  return value.size() >= lo && value.size() <= hi &&
         strlen(value.c_str()) == value.size();
}
void defaults(JsonDocument &d) {
  d.clear();
  d["schemaVersion"] = 2;
  d["revision"] = 0;
  d["role"] = "tx";
  d["route"] = "lora";
  d["activeProfile"] = 0;
  d["radio"] = "balanced";
  d["peers"].to<JsonArray>();
  d["activePeer"] = 0;
  d["wifi"]["ssid"] = "";
  d["wifi"]["configured"] = false;
  auto p = d["profiles"].to<JsonArray>().add<JsonObject>();
  p["name"] = "Presenter";
  p["output"] = "ble_keyboard";
  p["host"] = "";
  p["port"] = 8000;
  for (const char *k : {"previous", "next"}) {
    bool prev = strcmp(k, "previous") == 0;
    auto a = p[k].to<JsonObject>();
    a["key"] = prev ? 80 : 79;
    a["modifiers"] = 0;
    a["midi"]["type"] = "note";
    a["midi"]["channel"] = 1;
    a["midi"]["number"] = prev ? 60 : 62;
    a["midi"]["value"] = 100;
    a["osc"] = prev ? "/previous" : "/next";
    a["payload"] = prev ? "previous" : "next";
  }
}
bool validAction(JsonVariantConst a) {
  if (!keys(a, "|key||modifiers||midi||osc||payload|") ||
      !integer(a["key"], 0, 101) || !integer(a["modifiers"], 0, 255) ||
      !text(a["payload"], 0, 128) || !text(a["osc"], 1, 64))
    return false;
  String osc = a["osc"].as<String>();
  if (osc[0] != '/')
    return false;
  for (char c : osc)
    if (!isalnum((unsigned char)c) && c != '/' && c != '_' && c != '-')
      return false;
  auto m = a["midi"];
  return keys(m, "|type||channel||number||value|") &&
         oneOf(m["type"].as<String>(), "|note||cc||program|") &&
         integer(m["channel"], 1, 16) && integer(m["number"], 0, 127) &&
         integer(m["value"], 0, 127);
}
bool validConfig(JsonVariantConst c, const String &id) {
  if (!keys(c, "|schemaVersion||revision||role||route||activeProfile||profiles|"
               "|wifi||radio||peers||activePeer|") ||
      c.as<JsonObjectConst>().size() != 10 || !c["schemaVersion"].is<int>() ||
      c["schemaVersion"] != 2 || !c["revision"].is<uint32_t>() ||
      c["revision"].as<uint32_t>() == UINT32_MAX)
    return false;
  if (!oneOf(c["role"].as<String>(), "|tx||rx|") ||
      !oneOf(c["route"].as<String>(), "|lora||direct|") ||
      !oneOf(c["radio"].as<String>(), "|balanced||long_range|"))
    return false;
  auto ps = c["profiles"].as<JsonArrayConst>();
  if (!ps || ps.size() < 1 || ps.size() > 4 ||
      !integer(c["activeProfile"], 0, ps.size() - 1))
    return false;
  for (auto p : ps) {
    if (!keys(p, "|name||output||host||port||previous||next|") ||
        p.as<JsonObjectConst>().size() != 6 || !text(p["name"], 1, 24) ||
        !text(p["host"], 0, 63) || !integer(p["port"], 1, 65535) ||
        !validAction(p["previous"]) || !validAction(p["next"]))
      return false;
    String output = p["output"].as<String>(), host = p["host"].as<String>();
    if (!oneOf(output, "|ble_keyboard||ble_midi||osc_udp||udp||tcp|"))
      return false;
    for (char ch : host)
      if (!isalnum((unsigned char)ch) && ch != '.' && ch != '-')
        return false;
    if ((output == "osc_udp" || output == "udp" || output == "tcp") &&
        host.isEmpty())
      return false;
  }
  auto w = c["wifi"];
  if (!keys(w, "|ssid||configured|") || w.as<JsonObjectConst>().size() != 2 ||
      !text(w["ssid"], 0, 32) || !w["configured"].is<bool>())
    return false;
  auto peers = c["peers"].as<JsonArrayConst>();
  if (!peers || peers.size() > (c["role"] == "rx" ? 1 : 8) ||
      !integer(c["activePeer"], 0, peers.size() ? peers.size() - 1 : 0))
    return false;
  for (size_t i = 0; i < peers.size(); i++) {
    auto p = peers[i];
    if (!keys(p, "|deviceId||pairId||name||adminAuthorized|") ||
        p.as<JsonObjectConst>().size() != 4 || !text(p["deviceId"], 12, 12) ||
        !hex(p["deviceId"].as<String>(), 12) || p["deviceId"] == id ||
        !text(p["pairId"], 16, 16) || !hex(p["pairId"].as<String>(), 16) ||
        !text(p["name"], 1, 24) || !p["adminAuthorized"].is<bool>())
      return false;
    for (size_t j = 0; j < i; j++)
      if (peers[j]["deviceId"] == p["deviceId"] ||
          peers[j]["pairId"] == p["pairId"])
        return false;
  }
  return true;
}
} // namespace
struct ConfigService::Impl : BLECharacteristicCallbacks {
  struct Fragment {
    uint16_t size;
    uint8_t data[512];
  };
  QueueHandle_t queue = nullptr;
  ConfigFrames frames;
  BLECharacteristic *notify = nullptr;
  BLEServer *server = nullptr;
  uint32_t sentFragmentAt = 0;
  ConfigHooks hooks;
  JsonDocument committed, staged, pairs, stagedPairs, receipts;
  String owner, wifi, stagedWifi, session, device, version;
  char activeSlot = 0;
  bool remoteContext = false, editRemote = false, remotePending = false,
       remoteTaken = false, remoteAvailable = false, recoveryRequired = false;
  String remotePeer, remotePayload, contextPeer, remoteOperation,
      remoteEditPeer, remoteEditSession, cancelPeer, cancelSession;
  uint32_t remoteId = 0, remoteLogicalId = 0, nextRemoteId = 1, remoteAt = 0,
           remoteProgressAt = 0;
  uint8_t remoteStream = 0;
  uint16_t remoteConnection = 0xffff;
  uint32_t setupAt = 0, setupDuration = 0, editAt = 0, passkey = 0;
  bool setup = false, editing = false, authorized = false, dirtyStatus = true;
  unsigned failures = 0;
  std::atomic<bool> secure{false}, disconnected{false}, overflow{false};
  std::atomic<uint16_t> connection{0xffff};
  String response;
  size_t responseAt = 0;
  uint8_t responseStream = 0;
  uint16_t responseConnection = 0xffff;
  uint32_t responseAtTime = 0;
  void onWrite(BLECharacteristic *c) override {
    auto s = c->getValue();
    Fragment f{};
    if (s.length() > 512) {
      overflow = true;
      return;
    }
    f.size = s.length();
    memcpy(f.data, s.c_str(), f.size);
    if (xQueueSend(queue, &f, 0) != pdTRUE)
      overflow = true;
  }

#if defined(CONFIG_NIMBLE_ENABLED)
  void onWrite(BLECharacteristic *c, ble_gap_conn_desc *desc) override {
    if (desc && desc->conn_handle == connection && desc->sec_state.encrypted &&
        desc->sec_state.authenticated)
      onWrite(c);
  }
#elif defined(CONFIG_BLUEDROID_ENABLED)
  void onWrite(BLECharacteristic *c, esp_ble_gatts_cb_param_t *p) override {
    if (p && p->write.conn_id == connection && secure)
      onWrite(c);
  }
#endif
  void scheduleRemoteCancellation() {
    if (!remoteEditSession.isEmpty()) {
      cancelPeer = remoteEditPeer;
      cancelSession = remoteEditSession;
      remoteEditSession = "";
      remoteEditPeer = "";
    }
  }
  void stopEdit() {
    if (editing) {
      editing = false;
      staged.clear();
      stagedWifi = "";
      stagedPairs.clear();
      session = "";
      if (hooks.pauseOutput)
        hooks.pauseOutput(setup);
      dirtyStatus = true;
    }
  }
  String readSlot(Preferences &n, const char *slot, bool &failed) {
    size_t size = n.getBytesLength(slot);
    if (!size)
      return n.getString(slot); // Legacy string records remain readable.
    if (size > kConfigMaxMessage) {
      failed = true;
      return "";
    }
    std::unique_ptr<char[]> bytes(new (std::nothrow) char[size + 1]);
    if (!bytes || n.getBytes(slot, bytes.get(), size) != size ||
        memchr(bytes.get(), 0, size)) {
      failed = true;
      return "";
    }
    bytes[size] = 0;
    return String(bytes.get());
  }
  bool save(const JsonDocument &config, const String &o, const String &w,
            const JsonDocument &p, const JsonDocument *history = nullptr) {
    JsonDocument r;
    r["config"] = config;
    r["owner"] = o;
    r["wifi"] = w;
    r["pairs"] = p;
    r["receipts"] = history ? *history : receipts;
    String payload;
    serializeJson(r, payload);
    JsonDocument envelope;
    // Store one JSON object, avoiding a second layer of escaping and NVS's 4KB string limit.
    envelope["record"] = r;
    envelope["sha256"] = digest(payload);
    String bytes;
    serializeJson(envelope, bytes);
    if (bytes.length() > kConfigMaxMessage)
      return false;
    Preferences n;
    if (!n.begin("uc-config", false))
      return false;
    const char *slot = activeSlot == 'a' ? "b" : activeSlot == 'b' ? "a" :
        (config["revision"].as<uint32_t>() % 2) ? "b" : "a";
    // Only discard the inactive slot; the current complete record survives power loss and GC.
    if (n.isKey(slot) && !n.remove(slot)) {
      n.end();
      return false;
    }
    size_t written = n.putBytes(slot, bytes.c_str(), bytes.length());
    bool failed = false;
    String read = readSlot(n, slot, failed);
    n.end();
    if (failed) {
      recoveryRequired = true;
      dirtyStatus = true;
      if (hooks.pauseOutput)
        hooks.pauseOutput(true);
    }
    bool ok = !failed && written == bytes.length() && read == bytes;
    if (ok)
      activeSlot = slot[0];
    return ok;
  }
  void load() {
    defaults(committed);
    activeSlot = 0;
    Preferences n;
    if (!n.begin("uc-config", true))
      return;
    uint32_t highest = 0;
    bool found = false, present = false, unsupported = false, readFailed = false;
    for (const char *slot : {"a", "b"}) {
      String raw = readSlot(n, slot, readFailed);
      if (!raw.isEmpty())
        present = true;
      JsonDocument e, r;
      if (deserializeJson(e, raw))
        continue;
      String payload;
      if (e["record"].is<JsonObjectConst>()) {
        r.set(e["record"]);
        serializeJson(r, payload);
      } else {
        payload = e["payload"].as<String>();
        if (deserializeJson(r, payload))
          continue;
      }
      if (!equalSecret(e["sha256"].as<String>(), digest(payload)))
        continue;
      uint32_t rev = r["config"]["revision"];
      if (r["config"]["schemaVersion"] != 1 &&
          r["config"]["schemaVersion"] != 2) {
        unsupported = true;
        continue;
      }
      if (r["config"]["schemaVersion"] == 1) {
        auto c = r["config"];
        JsonDocument old;
        old.set(c["peer"]);
        c.remove("peer");
        c["schemaVersion"] = 2;
        c["activePeer"] = 0;
        auto peers = c["peers"].to<JsonArray>();
        if (!old.isNull()) {
          auto p = peers.add<JsonObject>();
          p["deviceId"] = old["deviceId"];
          p["pairId"] = old["pairId"];
          p["name"] = "Paired device";
          p["adminAuthorized"] = false;
          r["pairs"][old["deviceId"].as<String>()] = r["pair"];
        }
      }
      if (!validConfig(r["config"], device))
        continue;
      bool validPairs = true;
      for (auto p : r["config"]["peers"].as<JsonArrayConst>())
        if (!hex(r["pairs"][p["deviceId"].as<String>()].as<String>(), 64))
          validPairs = false;
      if (!r["owner"].is<const char *>() ||
          !(r["owner"] == "" || hex(r["owner"].as<String>(), 64)) ||
          !r["wifi"].is<const char *>())
        validPairs = false;
      String password = String(r["wifi"] | "");
      if ((!password.isEmpty() &&
           (password.length() < 8 || password.length() > 63)) ||
          r["config"]["wifi"]["configured"].as<bool>() != !password.isEmpty())
        validPairs = false;
      if (r["pairs"].as<JsonObjectConst>().size() !=
          r["config"]["peers"].size())
        validPairs = false;
      if (!r["receipts"].isNull() && !r["receipts"].is<JsonArrayConst>())
        validPairs = false;
      if (r["receipts"].size() > 1)
        validPairs = false;
      for (auto receipt : r["receipts"].as<JsonArrayConst>()) {
        if (!keys(receipt, "|controllerId||pairId||transactionId||"
                           "requestDigest||revision|") ||
            receipt.as<JsonObjectConst>().size() != 5 ||
            !hex(receipt["controllerId"].as<String>(), 12) ||
            !hex(receipt["pairId"].as<String>(), 16) ||
            !hex(receipt["transactionId"].as<String>(), 32) ||
            !hex(receipt["requestDigest"].as<String>(), 64) ||
            !receipt["revision"].is<uint32_t>() ||
            receipt["revision"].as<uint32_t>() > rev)
          validPairs = false;
      }
      if (!validPairs)
        continue;
      if (!found || rev > highest) {
        committed.set(r["config"]);
        owner = r["owner"].as<String>();
        wifi = r["wifi"].as<String>();
        pairs.set(r["pairs"]);
        receipts.set(r["receipts"]);
        highest = rev;
        found = true;
        activeSlot = slot[0];
      }
    }
    n.end();
    recoveryRequired = readFailed || unsupported || (present && !found);
    if (recoveryRequired) {
      defaults(committed);
      owner = "";
      wifi = "";
      pairs.clear();
      receipts.clear();
    }
  }
  String applyDigest(JsonVariantConst request) {
    JsonDocument normalized;
    normalized["domain"] = "UC-APPLY-2";
    normalized["transactionId"] = request["transactionId"];
    normalized["session"] = request["session"];
    normalized["expectedRevision"] = request["expectedRevision"];
    String bytes;
    serializeJson(normalized, bytes);
    return digest(bytes);
  }
  JsonObjectConst findReceipt(const String &transaction) {
    String pairId = committed["peers"][0]["pairId"].as<String>();
    for (auto receipt : receipts.as<JsonArrayConst>())
      if (receipt["controllerId"] == contextPeer &&
          receipt["pairId"] == pairId &&
          receipt["transactionId"] == transaction)
        return receipt.as<JsonObjectConst>();
    return JsonObjectConst();
  }
  void reply(uint32_t id, bool ok, const char *code = nullptr,
             JsonVariantConst result = JsonVariantConst()) {
    JsonDocument d;
    d["v"] = 2;
    d["id"] = id;
    d["ok"] = ok;
    if (ok) {
      if (result.isNull())
        d["result"].to<JsonObject>();
      else
        d["result"] = result;
    } else {
      d["error"]["code"] = code;
      d["error"]["message"] = code;
    }
    if (measureJson(d) > kConfigMaxMessage) {
      d.clear();
      d["v"] = 2;
      d["id"] = id;
      d["ok"] = false;
      d["error"]["code"] = "TOO_LARGE";
      d["error"]["message"] = "TOO_LARGE";
    }
    response = "";
    serializeJson(d, response);
    responseAt = 0;
    responseAtTime = millis();
  }
  void fail(uint32_t id, const char *code) { reply(id, false, code); }
  bool secret(JsonVariantConst s, String &target, bool isPair) {
    if (!keys(s, "|op||value|"))
      return false;
    String op = s["op"].as<String>();
    if (op == "keep" && s.as<JsonObjectConst>().size() == 1)
      return true;
    if (op == "clear" && s.as<JsonObjectConst>().size() == 1) {
      target = "";
      return true;
    }
    if (op != "replace" || s.as<JsonObjectConst>().size() != 2 ||
        !text(s["value"], isPair ? 64 : 8, isPair ? 64 : 63))
      return false;
    String v = s["value"].as<String>();
    if (isPair && !hex(v, 64))
      return false;
    target = v;
    return true;
  }
  void handle(const uint8_t *payload, size_t length) {
    JsonDocument q, out;
    uint32_t id = 0;
    if (!configUtf8(payload, length)) {
      fail(0, "INVALID");
      return;
    }
    if (deserializeJson(q, payload, length,
                        DeserializationOption::NestingLimit(12))) {
      fail(0, "INVALID");
      return;
    }
    if (!q["id"].is<uint32_t>() || (id = q["id"]) == 0 || id > 2147483647 ||
        !q["v"].is<int>() || q["v"] != 2 || !text(q["op"], 1, 16)) {
      fail(id, "INVALID");
      return;
    }
    String op = q["op"].as<String>();
    const char *allowed = "|v||id||op|";
    if (op == "receiver")
      allowed = "|v||id||op||deviceId||request|";
    else if (op == "claim")
      allowed = "|v||id||op||code|";
    else if (op == "authorize")
      allowed = "|v||id||op||token|";
    else if (op == "validate")
      allowed = "|v||id||op||session||config||secrets|";
    else if (op == "apply")
      allowed = "|v||id||op||session||expectedRevision||transactionId|";
    else if (op == "result")
      allowed = "|v||id||op||transactionId|";
    else if (op == "cancel")
      allowed = "|v||id||op||session|";
    if (!keys(q.as<JsonVariantConst>(), allowed)) {
      fail(id, "INVALID");
      return;
    }
    if (remoteContext &&
        (!oneOf(op, "|status||read||begin||validate||apply||cancel||result|") ||
         committed["role"] != "rx" || committed["peers"].size() != 1 ||
         committed["peers"][0]["deviceId"] != contextPeer ||
         committed["peers"][0]["adminAuthorized"] != true)) {
      fail(id, "REMOTE_DENIED");
      return;
    }
    if (op == "status") {
      out["deviceId"] = device;
      out["boardId"] = "thinknode_m5";
      out["firmwareVersion"] = version;
      out["schemaVersion"] = 2;
      out["protocolVersion"] = 2;
      out["revision"] = committed["revision"];
      out["setup"] = setup;
      out["editing"] = editing;
      out["claimed"] = !owner.isEmpty();
      out["recoveryRequired"] = recoveryRequired;
      auto c = out["capabilities"].to<JsonObject>();
      c["ble_config"] = true;
      c["lan_config"] = false;
      c["setup_hotspot"] = false;
      c["native_usb"] = false;
      c["maxProfiles"] = 4;
      c["remote_config"] = remoteAvailable;
      c["maxReceivers"] = 8;
      c["ota_session"] = !remoteContext && hooks.openUpdates != nullptr;
      out["remoteEditing"] = editRemote && editing;
      out["remotePending"] = remotePending;
      auto outputs = c["outputs"].to<JsonArray>();
      for (auto s : {"ble_keyboard", "ble_midi", "osc_udp", "udp", "tcp"})
        outputs.add(s);
      reply(id, true, nullptr, out.as<JsonVariantConst>());
      return;
    }
    if (recoveryRequired) {
      fail(id, "RECOVERY_REQUIRED");
      return;
    }
    if (!setup && !remoteContext) {
      fail(id, "SETUP_REQUIRED");
      return;
    }
    if (!secure && !remoteContext) {
      fail(id, "UNAUTHORIZED");
      return;
    }
    if (op == "claim" || op == "authorize") {
      bool good = false;
      if (op == "claim") {
        char code[7];
        snprintf(code, 7, "%06lu", (unsigned long)passkey);
        good = owner.isEmpty() && text(q["code"], 6, 6) &&
               equalSecret(q["code"].as<String>(), code);
      } else
        good = !owner.isEmpty() && text(q["token"], 64, 64) &&
               equalSecret(q["token"].as<String>(), owner);
      if (!good) {
        if (++failures >= 5) {
          setup = false;
          stopEdit();
          if (hooks.pauseOutput)
            hooks.pauseOutput(false);
          dirtyStatus = true;
        }
        fail(id, failures >= 5 ? "RATE_LIMITED" : "UNAUTHORIZED");
        return;
      }
      if (op == "claim") {
        if (committed["revision"].as<uint32_t>() >= UINT32_MAX - 1) {
          fail(id, "INVALID");
          return;
        }
        String token = randomHex(32);
        JsonDocument next;
        next.set(committed);
        next["revision"] = committed["revision"].as<uint32_t>() + 1;
        if (!save(next, token, wifi, pairs)) {
          fail(id, "PERSIST_FAILED");
          return;
        }
        committed.set(next);
        owner = token;
        out["token"] = token;
        dirtyStatus = true;
      }
      authorized = true;
      reply(id, true, nullptr, out.as<JsonVariantConst>());
      return;
    }
    if (!authorized && !remoteContext) {
      fail(id, "UNAUTHORIZED");
      return;
    }
    if (op == "updates") {
      if (editing || remotePending || !remoteEditSession.isEmpty() || !cancelSession.isEmpty()) {
        fail(id, "BUSY");
        return;
      }
      if (!hooks.openUpdates) {
        fail(id, "UNSUPPORTED");
        return;
      }
      if (!hooks.openUpdates()) {
        fail(id, "BUSY");
        return;
      }
      out["requested"] = true;
      reply(id, true, nullptr, out.as<JsonVariantConst>());
      return;
    }
    if (op == "receiver") {
      String target = q["deviceId"].as<String>();
      bool found = false;
      for (auto p : committed["peers"].as<JsonArrayConst>())
        if (p["deviceId"] == target && p["adminAuthorized"] == true)
          found = true;
      auto request = q["request"];
      if (committed["role"] != "tx" || !found || !request.is<JsonObject>() ||
          !request["v"].isUnbound() || !request["id"].isUnbound() ||
          !oneOf(request["op"].as<String>(),
                 "|status||read||begin||validate||apply||cancel||result|")) {
        fail(id, "REMOTE_DENIED");
        return;
      }
      if (!remoteAvailable) {
        fail(id, "UNSUPPORTED");
        return;
      }
      if (remotePending || editing) {
        fail(id, "BUSY");
        return;
      }
      if (nextRemoteId >= 2147483647) {
        fail(id, "RATE_LIMITED");
        return;
      }
      uint32_t bridgeId = nextRemoteId++;
      JsonDocument envelope;
      envelope.set(request);
      envelope["v"] = 2;
      envelope["id"] = bridgeId;
      remotePayload = "";
      serializeJson(envelope, remotePayload);
      if (remotePayload.length() > kConfigMaxMessage) {
        fail(id, "TOO_LARGE");
        return;
      }
      remotePending = true;
      remoteTaken = false;
      remotePeer = target;
      remoteOperation = request["op"].as<String>();
      remoteId = bridgeId;
      remoteLogicalId = id;
      remoteAt = remoteProgressAt = millis();
      remoteConnection = connection.load();
      remoteStream = responseStream;
      setupAt = millis();
      setupDuration = 120000;
      dirtyStatus = true;
      return;
    }
    if (op == "read") {
      out["config"] = committed;
      reply(id, true, nullptr, out.as<JsonVariantConst>());
      return;
    }
    if (op == "begin") {
      if (editing) {
        fail(id, "BUSY");
        return;
      }
      editing = true;
      editAt = millis();
      editRemote = remoteContext;
      session = randomHex(8);
      staged.set(committed);
      stagedWifi = wifi;
      stagedPairs.set(pairs);
      if (hooks.pauseOutput)
        hooks.pauseOutput(true);
      dirtyStatus = true;
      out["session"] = session;
      out["config"] = committed;
      out["expiresIn"] = 120;
      reply(id, true, nullptr, out.as<JsonVariantConst>());
      return;
    }
    if (op == "revoke") {
      if (committed["revision"].as<uint32_t>() >= UINT32_MAX - 1) {
        fail(id, "INVALID");
        return;
      }
      JsonDocument next;
      next.set(committed);
      next["revision"] = committed["revision"].as<uint32_t>() + 1;
      if (!save(next, "", wifi, pairs)) {
        fail(id, "PERSIST_FAILED");
        return;
      }
      committed.set(next);
      owner = "";
      authorized = false;
      stopEdit();
      setup = false;
      if (hooks.pauseOutput)
        hooks.pauseOutput(false);
      dirtyStatus = true;
      if (hooks.clearBonds)
        hooks.clearBonds();
      reply(id, true);
      return;
    }
    if (op == "result") {
      if (!remoteContext || !hex(q["transactionId"].as<String>(), 32)) {
        fail(id, "INVALID");
        return;
      }
      auto receipt = findReceipt(q["transactionId"].as<String>());
      out["transactionId"] = q["transactionId"];
      out["state"] = receipt.isNull() ? "unknown" : "applied";
      if (!receipt.isNull())
        out["revision"] = receipt["revision"];
      reply(id, true, nullptr, out.as<JsonVariantConst>());
      return;
    }
    if (op == "apply" && remoteContext) {
      if (!hex(q["transactionId"].as<String>(), 32)) {
        fail(id, "INVALID");
        return;
      }
      auto receipt = findReceipt(q["transactionId"].as<String>());
      if (!receipt.isNull()) {
        if (receipt["requestDigest"] != applyDigest(q.as<JsonVariantConst>())) {
          fail(id, "TRANSACTION_CONFLICT");
          return;
        }
        out["revision"] = receipt["revision"];
        out["transactionId"] = q["transactionId"];
        out["duplicate"] = true;
        reply(id, true, nullptr, out.as<JsonVariantConst>());
        return;
      }
    }
    if (!oneOf(op, "|validate||apply||cancel|")) {
      fail(id, "UNSUPPORTED");
      return;
    }
    if (!editing || q["session"] != session || editRemote != remoteContext) {
      fail(id, "SESSION_EXPIRED");
      return;
    }
    if (op == "cancel") {
      stopEdit();
      reply(id, true);
      return;
    }
    if (op == "validate") {
      if (!validConfig(q["config"], device) ||
          q["config"]["revision"] != committed["revision"] ||
          !keys(q["secrets"], "|wifi||pairs|") ||
          q["secrets"].as<JsonObjectConst>().size() != 2 ||
          !q["secrets"]["pairs"].is<JsonArrayConst>()) {
        fail(id, "INVALID");
        return;
      }
      if (remoteContext &&
          (q["config"]["role"] != committed["role"] ||
           q["config"]["radio"] != committed["radio"] ||
           q["config"]["peers"].as<JsonVariantConst>() !=
               committed["peers"].as<JsonVariantConst>() ||
           q["config"]["activePeer"] != committed["activePeer"])) {
        fail(id, "REMOTE_DENIED");
        return;
      }
      String w = wifi;
      JsonDocument candidatePairs;
      candidatePairs.set(pairs);
      if (!secret(q["secrets"]["wifi"], w, false) ||
          (q["config"]["wifi"]["ssid"].as<String>().isEmpty() &&
           !w.isEmpty()) ||
          q["config"]["wifi"]["configured"].as<bool>() != !w.isEmpty()) {
        fail(id, "INVALID");
        return;
      }
      JsonDocument seen;
      for (auto operation : q["secrets"]["pairs"].as<JsonArrayConst>()) {
        String peerId = operation["deviceId"].as<String>();
        if (!keys(operation, "|deviceId||op||value|") || !hex(peerId, 12) ||
            seen[peerId].as<bool>() ||
            (remoteContext && operation["op"] != "keep")) {
          fail(id, "INVALID");
          return;
        }
        bool known = false;
        for (auto p : committed["peers"].as<JsonArrayConst>())
          if (p["deviceId"] == peerId)
            known = true;
        for (auto p : q["config"]["peers"].as<JsonArrayConst>())
          if (p["deviceId"] == peerId)
            known = true;
        if (!known) {
          fail(id, "INVALID");
          return;
        }
        seen[peerId] = true;
        JsonDocument op;
        op.set(operation);
        op.remove("deviceId");
        String value = String(candidatePairs[peerId] | "");
        if (!secret(op.as<JsonVariantConst>(), value, true)) {
          fail(id, "INVALID");
          return;
        }
        if (value.isEmpty())
          candidatePairs.remove(peerId);
        else
          candidatePairs[peerId] = value;
      }
      size_t count = 0;
      for (auto p : q["config"]["peers"].as<JsonArrayConst>()) {
        String peerId = p["deviceId"].as<String>();
        if (!hex(candidatePairs[peerId].as<String>(), 64)) {
          fail(id, "INVALID");
          return;
        }
        count++;
      }
      for (auto p : candidatePairs.as<JsonObjectConst>())
        for (auto other : candidatePairs.as<JsonObjectConst>())
          if (strcmp(p.key().c_str(), other.key().c_str()) &&
              p.value() == other.value()) {
            fail(id, "INVALID");
            return;
          }
      if (candidatePairs.as<JsonObjectConst>().size() != count) {
        fail(id, "INVALID");
        return;
      }
      staged.set(q["config"]);
      stagedWifi = w;
      stagedPairs.set(candidatePairs);
      out["valid"] = true;
      reply(id, true, nullptr, out.as<JsonVariantConst>());
      return;
    }
    if (!q["expectedRevision"].is<uint32_t>() ||
        q["expectedRevision"] != committed["revision"]) {
      fail(id, "STALE_REVISION");
      return;
    }
    uint32_t revision = committed["revision"];
    if (revision >= UINT32_MAX - 1) {
      fail(id, "INVALID");
      return;
    }
    staged["revision"] = revision + 1;
    JsonDocument history;
    history.set(receipts);
    if (remoteContext) {
      auto list = history.as<JsonArray>();
      if (list.isNull())
        list = history.to<JsonArray>();
      if (list.size() >= 1)
        list.remove(0);
      auto receipt = list.add<JsonObject>();
      receipt["controllerId"] = contextPeer;
      receipt["pairId"] = committed["peers"][0]["pairId"];
      receipt["transactionId"] = q["transactionId"];
      receipt["requestDigest"] = applyDigest(q.as<JsonVariantConst>());
      receipt["revision"] = revision + 1;
    }
    if (!save(staged, owner, stagedWifi, stagedPairs, &history)) {
      staged["revision"] = revision;
      fail(id, "PERSIST_FAILED");
      return;
    }
    committed.set(staged);
    receipts.set(history);
    wifi = stagedWifi;
    pairs.set(stagedPairs);
    stopEdit();
    out["revision"] = committed["revision"];
    if (remoteContext)
      out["transactionId"] = q["transactionId"];
    reply(id, true, nullptr, out.as<JsonVariantConst>());
    if (hooks.changed)
      hooks.changed();
    dirtyStatus = true;
  }
};
bool ConfigService::begin(BLEServer *server, const String &device,
                          const String &version, ConfigHooks hooks) {
  if (impl_ || !server || !hex(device, 12))
    return false;
  impl_ = new Impl();
  auto &i = *impl_;
  i.device = device;
  i.version = version;
  i.hooks = hooks;
  i.server = server;
  i.queue = xQueueCreate(16, sizeof(Impl::Fragment));
  if (!i.queue)
    return false;
  i.load();
  if (i.recoveryRequired && i.hooks.pauseOutput)
    i.hooks.pauseOutput(true);
  auto service = server->createService(kConfigServiceUuid);
  auto write = service->createCharacteristic(
      kConfigWriteUuid, BLECharacteristic::PROPERTY_WRITE |
                            BLECharacteristic::PROPERTY_WRITE_ENC |
                            BLECharacteristic::PROPERTY_WRITE_AUTHEN);
#if CONFIG_BLUEDROID_ENABLED
  write->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
#endif
  write->setCallbacks(impl_);
  i.notify = service->createCharacteristic(kConfigNotifyUuid,
                                           BLECharacteristic::PROPERTY_NOTIFY);
  service->start();
  return true;
}
void ConfigService::poll() {
  if (!impl_)
    return;
  auto &i = *impl_;
  uint32_t now = millis();
  if (i.disconnected.exchange(false)) {
    i.authorized = false;
    if (!i.editRemote)
      i.stopEdit();
    i.scheduleRemoteCancellation();
    i.remotePending = false;
    i.remotePayload = "";
    if (i.setup) {
      i.setupAt = now;
      i.setupDuration = 120000;
      i.dirtyStatus = true;
    }
    i.frames.clear();
    xQueueReset(i.queue);
    i.response = "";
  }
  if (i.setup && now - i.setupAt >= i.setupDuration)
    exitSetup();
  if (i.editing && uint32_t(now - i.editAt) >= 120000)
    i.stopEdit();
  if (i.frames.used && (uint32_t(now - i.frames.touched) >= 10000 ||
                        uint32_t(now - i.frames.started) >= 60000))
    i.frames.clear();
  if (i.overflow.exchange(false)) {
    i.frames.clear();
    i.fail(0, "TOO_LARGE");
    xQueueReset(i.queue);
  }
  if (i.remotePending && (uint32_t(now - i.remoteAt) >= 3600000 ||
                          uint32_t(now - i.remoteProgressAt) >= 120000)) {
    i.responseStream = i.remoteStream;
    i.responseConnection = i.remoteConnection;
    i.fail(i.remoteLogicalId, "REMOTE_TIMEOUT");
    i.remotePending = false;
    i.remotePayload = "";
  }
  if (i.response.isEmpty() && !i.remotePending) {
    Impl::Fragment f;
    unsigned processed = 0;
    while (processed++ < 16 && xQueueReceive(i.queue, &f, 0) == pdTRUE) {
      int r = i.frames.feed(f.data, f.size, now);
      if (r < 0) {
        i.fail(0, r == -2 ? "TOO_LARGE" : "FRAMING");
        break;
      }
      if (r == 1) {
        i.responseStream = i.frames.stream;
        i.responseConnection = i.connection.load();
        i.handle(i.frames.bytes, i.frames.used);
        i.frames.clear();
        break;
      }
    }
  }
  if (!i.response.isEmpty()) {
    if (now - i.responseAtTime > 10000) {
      i.response = "";
      i.authorized = false;
      i.stopEdit();
    } else {
#if !defined(UC_CONFIG_HOST_TEST)
      if (uint32_t(now - i.sentFragmentAt) < 5)
        return;
#endif
      i.sentFragmentAt = now;
      size_t att = 20;
#if !defined(UC_CONFIG_HOST_TEST)
      att = min(
          (size_t)512,
          (size_t)max(20, (int)i.server->getPeerMTU(i.responseConnection) - 3));
#endif
      uint8_t f[512] = {0x55,
                        0x43,
                        1,
                        i.responseStream,
                        (uint8_t)i.responseAt,
                        (uint8_t)(i.responseAt >> 8),
                        (uint8_t)i.response.length(),
                        (uint8_t)(i.response.length() >> 8)};
      size_t n = min(att - 8, i.response.length() - i.responseAt);
      memcpy(f + 8, i.response.c_str() + i.responseAt, n);
      bool submitted = false;
#if defined(CONFIG_NIMBLE_ENABLED) && !defined(UC_CONFIG_HOST_TEST)
      // Arduino BLE notify() broadcasts to every subscriber. Target the
      // authorized request connection explicitly so a separate HID client
      // cannot receive tokens.
      ble_gap_conn_desc desc{};
      if (i.responseConnection == i.connection &&
          ble_gap_conn_find(i.responseConnection, &desc) == 0 &&
          desc.sec_state.encrypted && desc.sec_state.authenticated) {
        os_mbuf *buffer = ble_hs_mbuf_from_flat(f, n + 8);
        if (buffer)
          submitted =
              ble_gatts_notify_custom(i.responseConnection,
                                      i.notify->getHandle(), buffer) == 0;
      }
#elif defined(UC_CONFIG_HOST_TEST)
      i.notify->setValue(f, n + 8);
      i.notify->notify();
      submitted = true;
#endif
      if (!submitted)
        return; // Retry congestion until the bounded response deadline.
      i.responseAt += n;
      if (i.responseAt >= i.response.length())
        i.response = "";
    }
  }
  if (i.dirtyStatus) {
    i.dirtyStatus = false;
    if (i.hooks.statusChanged)
      i.hooks.statusChanged(status());
  }
}
void ConfigService::enterSetup(uint32_t duration) {
  if (!impl_)
    return;
  auto &i = *impl_;
  i.stopEdit();
  i.authorized = false;
  i.setup = true;
  i.setupAt = millis();
  i.setupDuration = min(duration, (uint32_t)120000);
  i.passkey = 100000 + esp_random() % 900000;
  i.failures = 0;
  i.dirtyStatus = true;
  if (i.hooks.pauseOutput)
    i.hooks.pauseOutput(true);
}
void ConfigService::exitSetup() {
  if (!impl_)
    return;
  auto &i = *impl_;
  i.setup = false;
  i.authorized = false;
  if (!i.editRemote)
    i.stopEdit();
  i.scheduleRemoteCancellation();
  i.remotePending = false;
  i.remotePayload = "";
  i.frames.clear();
  i.dirtyStatus = true;
  if (i.hooks.pauseOutput)
    i.hooks.pauseOutput(i.editing || i.recoveryRequired);
}
void ConfigService::onSecurity(uint16_t c, bool encrypted, bool authenticated) {
  if (!impl_)
    return;
  if (impl_->connection != c)
    impl_->disconnected = true;
  impl_->connection = c;
  impl_->secure = encrypted && authenticated;
  if (!impl_->secure)
    impl_->disconnected = true;
}
void ConfigService::onDisconnect(uint16_t c) {
  if (impl_ && impl_->connection == c) {
    impl_->secure = false;
    impl_->disconnected = true;
  }
}
const JsonDocument &ConfigService::settings() const { return impl_->committed; }
ConfigStatus ConfigService::status() const {
  ConfigStatus s;
  if (impl_) {
    s.setup = impl_->setup;
    s.editing = impl_->editing;
    s.remoteEditing = impl_->editing && impl_->editRemote;
    s.remotePending = impl_->remotePending;
    s.remoteDeviceId = impl_->remotePeer;
    s.claimed = !impl_->owner.isEmpty();
    s.recoveryRequired = impl_->recoveryRequired;
    s.revision = impl_->committed["revision"];
    s.passkey = impl_->passkey;
    s.deviceId = impl_->device;
  }
  return s;
}
bool ConfigService::outputPaused() const {
  return impl_ && (impl_->editing || impl_->setup || impl_->recoveryRequired);
}
String ConfigService::wifiPassword() const {
  return impl_ ? impl_->wifi : String();
}
String ConfigService::pairKeyHex(const String &peerId) const {
  if (!impl_)
    return String();
  String target = peerId;
  if (target.isEmpty())
    target =
        impl_
            ->committed["peers"][impl_->committed["activePeer"].as<unsigned>()]
                       ["deviceId"]
            .as<String>();
  return String(impl_->pairs[target] | "");
}
bool ConfigService::takeRemoteRequest(String &receiverId, String &request,
                                      uint32_t &correlation) {
  if (!impl_ || !impl_->remotePending || impl_->remoteTaken)
    return false;
  auto &i = *impl_;
  receiverId = i.remotePeer;
  request = i.remotePayload;
  correlation = i.remoteId;
  i.remoteTaken = true;
  return true;
}
void ConfigService::setRemoteTransportAvailable(bool available) {
  if (impl_)
    impl_->remoteAvailable = available;
}
bool ConfigService::takeRemoteCancellation(String &peer, String &session) {
  if (!impl_ || impl_->cancelSession.isEmpty())
    return false;
  peer = impl_->cancelPeer;
  session = impl_->cancelSession;
  impl_->cancelPeer = "";
  impl_->cancelSession = "";
  return true;
}
void ConfigService::noteRemoteProgress(const String &peerId) {
  if (!impl_)
    return;
  auto &i = *impl_;
  uint32_t now = millis();
  if (i.remotePending && i.remotePeer == peerId) {
    i.remoteProgressAt = now;
    if (i.setup && i.authorized) {
      i.setupAt = now;
      i.setupDuration = 120000;
    }
  }
  if (i.editing && i.editRemote &&
      i.committed["peers"][0]["deviceId"] == peerId)
    i.editAt = now;
}
bool ConfigService::remoteRequestActive(uint32_t correlation) const {
  return impl_ && impl_->remotePending &&
         (!correlation || correlation == impl_->remoteId);
}
void ConfigService::completeRemoteRequest(uint32_t correlation,
                                          const String &response) {
  if (!impl_ || !impl_->remotePending || correlation != impl_->remoteId)
    return;
  auto &i = *impl_;
  JsonDocument received, result;
  i.responseStream = i.remoteStream;
  i.responseConnection = i.remoteConnection;
  if (response.length() > kConfigMaxMessage ||
      deserializeJson(received, response) || received["v"] != 2 ||
      received["id"] != correlation || !received["ok"].is<bool>())
    i.fail(i.remoteLogicalId, "REMOTE_UNAVAILABLE");
  else {
    if (received["ok"] == true && i.remoteOperation == "begin" &&
        hex(received["result"]["session"].as<String>(), 16)) {
      i.remoteEditPeer = i.remotePeer;
      i.remoteEditSession = received["result"]["session"].as<String>();
    }
    if (received["ok"] == true &&
        (i.remoteOperation == "apply" || i.remoteOperation == "cancel")) {
      i.remoteEditPeer = "";
      i.remoteEditSession = "";
    }
    result["deviceId"] = i.remotePeer;
    result["response"] = received;
    i.reply(i.remoteLogicalId, true, nullptr, result.as<JsonVariantConst>());
  }
  i.remotePending = false;
  i.remotePayload = "";
}
String ConfigService::processRemoteRequest(const String &controllerId,
                                           const String &request) {
  if (!impl_)
    return String();
  auto &i = *impl_;
  String saved = i.response;
  size_t at = i.responseAt;
  uint32_t timestamp = i.responseAtTime;
  i.remoteContext = true;
  i.contextPeer = controllerId;
  if (request.length() > kConfigMaxMessage)
    i.fail(0, "TOO_LARGE");
  else
    i.handle((const uint8_t *)request.c_str(), request.length());
  String result = i.response;
  JsonDocument decoded;
  if (!deserializeJson(decoded, result) && decoded["ok"] == true && i.editing &&
      i.editRemote)
    i.editAt = millis();
  i.response = saved;
  i.responseAt = at;
  i.responseAtTime = timestamp;
  i.remoteContext = false;
  i.contextPeer = "";
  return result;
}
bool ConfigService::factoryReset() {
  if (!impl_)
    return false;
  Preferences n;
  if (!n.begin("uc-config", false))
    return false;
  bool ok = n.clear();
  n.end();
  if (!ok)
    return false;
  auto &i = *impl_;
  i.stopEdit();
  defaults(i.committed);
  i.owner = "";
  i.wifi = "";
  i.pairs.clear();
  i.receipts.clear();
  i.activeSlot = 0;
  i.recoveryRequired = false;
  i.remotePending = false;
  i.remotePayload = "";
  i.remoteEditSession = "";
  i.cancelSession = "";
  i.cancelPeer = "";
  i.authorized = false;
  i.frames.clear();
  i.response = "";
  xQueueReset(i.queue);
  i.secure = false;
  i.connection = 0xffff;
  if (i.hooks.clearBonds)
    i.hooks.clearBonds();
  enterSetup();
  if (i.hooks.changed)
    i.hooks.changed();
  return true;
}
} // namespace uc
