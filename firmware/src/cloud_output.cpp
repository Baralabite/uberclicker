#include "cloud_output.h"
#include "cloud_clock.h"
#include "output_safety.h"
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <atomic>
#include <time.h>

extern const uint8_t cloud_ca_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t cloud_ca_end[] asm("_binary_x509_crt_bundle_end");

namespace cloud_output {
namespace {
struct Config { char room[33], token[65], name[25]; bool enabled; };
struct LinkStatus { char code[9]; };
QueueHandle_t linkStatus;
void linkCode(const char *code) { LinkStatus status{}; strlcpy(status.code, code, sizeof(status.code)); if (linkStatus) xQueueOverwrite(linkStatus, &status); }
bool validHex(const String &value, unsigned length) { if(value.length()!=length)return false; for(char c:value)if(!isxdigit(c))return false; return true; }
bool accountCredentials(Config &config) {
  Preferences preferences; if (!preferences.begin("uc-cloud", false)) return false;
  String saved=preferences.getString("identity", "");
  if(saved.isEmpty()) {
    char bytes[98]; for(unsigned i=0;i<97;++i)bytes[i]="0123456789abcdef"[esp_random()&15]; bytes[32]=':'; bytes[97]=0;
    saved=bytes; if(preferences.putString("identity",saved)!=saved.length()){preferences.end();return false;}
  }
  preferences.end();
  if(saved.length()!=97||saved[32]!=':'||!validHex(saved.substring(0,32),32)||!validHex(saved.substring(33),64))return false;
  strlcpy(config.room,saved.substring(0,32).c_str(),sizeof(config.room));strlcpy(config.token,saved.substring(33).c_str(),sizeof(config.token));return true;
}
bool registerAccount(const Config &config) {
  NetworkClientSecure client; client.setCACertBundle(cloud_ca_start,cloud_ca_end-cloud_ca_start);
  HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(5000);
  if(!http.begin(client,"https://api.uberclicker.com/v1/clickers"))return false;
  http.addHeader("Content-Type","application/json");
  JsonDocument body;body["id"]=config.room;body["token"]=config.token;body["name"]=config.name;String payload;serializeJson(body,payload);
  const int status=http.POST(payload);http.end();return status==200||status==201;
}
struct Job { bool next; uint32_t at, epoch; };
QueueHandle_t changes, jobs, results;
std::atomic<bool> paused{true}, online{false}, active{false};
std::atomic<uint32_t> epoch{0};
WebSocketsClient socket;
Clock clockState;
bool pending = false, queued = false;
Job sent{}, deferred{};
uint32_t seq = 0, syncStarted = 0, lastSync = 0;
String session, expected;

void finish(outputs::Result result) {
  pending = false; queued = false;
  xQueueOverwrite(results, &result);
}
void event(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_DISCONNECTED) {
    online = false; clockState.synced = false; ++epoch; linkCode("");
    if (pending) finish(outputs::Result::Unknown);
    else if (queued) finish(outputs::Result::Rejected);
    return;
  }
  if (type != WStype_TEXT) return;
  if (length > 512) { socket.disconnect(); return; }
  JsonDocument message;
  if (deserializeJson(message, payload, length) || message["v"] != 1) { socket.disconnect(); return; }
  if (message["type"] == "pairing") {
    const char *code=message["code"]|"";if(strlen(code)==8)linkCode(code);
  } else if (message["type"] == "linked") { linkCode("");
  } else if (message["type"] == "ready") {
    session = message["session"].as<String>(); seq = 0;
    if (session.length() != 32) { socket.disconnect(); return; }
    syncStarted = millis(); lastSync = syncStarted;
    char sync[96]; snprintf(sync, sizeof(sync), "{\"v\":1,\"type\":\"sync\",\"nonce\":%lu}", (unsigned long)syncStarted);
    socket.sendTXT(sync);
  } else if (message["type"] == "sync" && message["nonce"].is<uint32_t>() && message["nonce"] == syncStarted && message["now"].is<int64_t>()) {
    online = clockState.sync(message["now"].as<int64_t>(), syncStarted, millis());
  } else if (message["type"] == "result" && pending && message["id"] == expected) {
    const char *status = message["status"] | "";
    finish(!strcmp(status, "submitted") ? outputs::Result::Submitted : !strcmp(status, "rejected") ? outputs::Result::Rejected : outputs::Result::Unknown);
  }
}
void task(void *) {
  Config config{}; bool started = false, ntp = false, account = false, registered = false; uint32_t retryAt=0;
  char transmitterId[13];
  snprintf(transmitterId, sizeof(transmitterId), "%012llx", (unsigned long long)(ESP.getEfuseMac() & 0xffffffffffffULL));
  socket.onEvent(event); socket.setReconnectInterval(3000); socket.enableHeartbeat(20000, 5000, 2);
  for (;;) {
    Config next;
    if (xQueueReceive(changes, &next, 0) == pdPASS) {
      socket.disconnect(); started = false; online = false; clockState.synced = false;
      if (pending) finish(outputs::Result::Unknown);
      else if (queued) finish(outputs::Result::Rejected);
      config = next; account = config.enabled && !config.room[0]; registered = !account; retryAt=0; linkCode("");
      if(account && !accountCredentials(config))config.enabled=false;
    }
    if (WiFi.status() != WL_CONNECTED || !config.enabled) {
      if (started) socket.disconnect();
      started = false; online = false; clockState.synced = false;
    } else if (time(nullptr) < 1704067200) {
      if (!ntp) { configTime(0, 0, "time.cloudflare.com", "pool.ntp.org"); ntp = true; }
    } else {
      if (!registered && (!retryAt || int32_t(millis()-retryAt)>=0)) { registered=registerAccount(config); retryAt=millis()+60000; }
      if (!registered) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
      if (!started) {
        char path[96], header[96];
        snprintf(path, sizeof(path), "/v1/rooms/%s/tx", config.room);
        snprintf(header, sizeof(header), "Authorization: Bearer %s\r\n", config.token);
        // Always verify the server certificate using the ESP32's bundled trust roots.
        socket.beginSslWithBundle("api.uberclicker.com", 443, path, cloud_ca_start, cloud_ca_end - cloud_ca_start, "");
        socket.setExtraHeaders(header); started = true;
      }
      socket.loop();
    }
    if (pending && !output_safety::current(paused, sent.epoch, epoch, sent.at, millis())) finish(outputs::Result::Unknown);
    if (!pending && !queued && xQueueReceive(jobs, &deferred, 0) == pdPASS) {
      queued = true;
      // Refresh a stale clock only on a real click. Idle connections send free protocol pings only.
      if (socket.isConnected() && uint32_t(millis() - lastSync) > 300000) {
        online = false; clockState.synced = false; syncStarted = millis(); lastSync = syncStarted;
        char sync[96]; snprintf(sync, sizeof(sync), "{\"v\":1,\"type\":\"sync\",\"nonce\":%lu}", (unsigned long)syncStarted);
        if (!socket.sendTXT(sync)) finish(outputs::Result::Rejected);
      }
    }
    if (queued && !output_safety::current(paused, deferred.epoch, epoch, deferred.at, millis())) finish(outputs::Result::Rejected);
    if (queued && online) {
      Job job=deferred; queued=false;
      int64_t at = clockState.at(job.at, millis());
      if (!online || !socket.isConnected() || !at || seq == UINT32_MAX ||
          !output_safety::current(paused, job.epoch, epoch, job.at, millis())) {
        finish(outputs::Result::Rejected);
      } else {
        ++seq; expected = session + ":" + String(seq); sent = job; pending = true;
        char message[192]; snprintf(message, sizeof(message), "{\"v\":1,\"type\":\"click\",\"seq\":%lu,\"button\":\"%s\",\"sentAt\":%lld,\"transmitterId\":\"%s\"}", (unsigned long)seq, job.next ? "next" : "previous", (long long)at, transmitterId);
        if (!socket.sendTXT(message)) finish(outputs::Result::Unknown);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}
bool begin() {
  linkStatus = xQueueCreate(1,sizeof(LinkStatus));
  changes = xQueueCreate(1, sizeof(Config)); jobs = xQueueCreate(1, sizeof(Job)); results = xQueueCreate(1, sizeof(outputs::Result));
  if (linkStatus && changes && jobs && results && xTaskCreate(task, "cloud", 8192, nullptr, 1, nullptr) == pdPASS) return true;
  jobs = nullptr; return false;
}
void configure(const String &room, const String &token, bool enabled, const String &name) {
  ++epoch; online = false;
  Config config{}; strlcpy(config.room, room.c_str(), sizeof(config.room)); strlcpy(config.token, token.c_str(), sizeof(config.token));
  strlcpy(config.name,name.isEmpty()?"M5 clicker":name.c_str(),sizeof(config.name));
  config.enabled = enabled && ((room.isEmpty() && token.isEmpty()) || (room.length() == 32 && token.length() == 64));
  if (changes) xQueueOverwrite(changes, &config);
}
void pause(bool value) { if (paused.exchange(value) != value) ++epoch; }
outputs::Result submit(bool next, uint32_t issuedAt) {
  if (!jobs || !results || paused || !online || active.exchange(true)) return outputs::Result::Rejected;
  Job job{next, issuedAt, epoch};
  if (xQueueSend(jobs, &job, 0) != pdPASS) { active = false; return outputs::Result::Rejected; }
  return outputs::Result::Pending;
}
bool takeResult(outputs::Result &result) {
  if (!results || xQueueReceive(results, &result, 0) != pdPASS) return false;
  active = false; return true;
}
bool connected() { return online; }
void pairingCode(char *code, size_t size) { LinkStatus status{}; if(linkStatus)xQueuePeek(linkStatus,&status,0);strlcpy(code,status.code,size); }
bool forgetAccount() { Preferences preferences;if(!preferences.begin("uc-cloud",false))return false;const bool cleared=preferences.clear();preferences.end();return cleared; }
}
