#include "ota_update.h"
#include "ota_manifest.h"
#include "config_service.h"
#include <memory>
#include <Arduino.h>
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <time.h>

namespace ota_update {
namespace {
constexpr char Base[] = "https://uberclicker.com/flash/releases/";
enum class Job : uint8_t { Check, Install };
QueueHandle_t jobs = nullptr, results = nullptr;
Status current;
uint32_t restartAt = 0;

struct Http {
  esp_http_client_handle_t client = nullptr;
  ~Http() { if (client) esp_http_client_cleanup(client); }
  int64_t open(const char *url) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 5000;
    config.disable_auto_redirect = true; // Both assets and feed stay on the verified Pages origin.
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    client = esp_http_client_init(&config);
    if (!client || esp_http_client_open(client, 0) != ESP_OK) return -1;
    int64_t size = esp_http_client_fetch_headers(client);
    return esp_http_client_get_status_code(client) == 200 ? size : -1;
  }
};
void report(State state, const char *detail, const char *version = "") {
  Status result; result.state = state;
  strlcpy(result.detail, detail, sizeof(result.detail));
  strlcpy(result.version, version, sizeof(result.version));
  xQueueOverwrite(results, &result);
}
bool layoutMatches() {
  for (auto &expected : Partitions) {
    auto p = esp_partition_find_first(static_cast<esp_partition_type_t>(expected.type), static_cast<esp_partition_subtype_t>(expected.subtype), nullptr);
    if (!p || p->address != expected.offset || p->size != expected.size) return false;
  }
  auto running = esp_ota_get_running_partition();
  auto next = esp_ota_get_next_update_partition(nullptr);
  return running && next && running->address != next->address && next->size == SlotSize;
}
const char *ready() {
  if (!layoutMatches()) return "USB OTA INSTALL REQUIRED";
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && uint32_t(millis()-start) < 15000) delay(100);
  if (WiFi.status() != WL_CONNECTED) return "SET WI-FI IN APP SETUP";
  if (time(nullptr) < 1704067200) {
    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
    start = millis();
    while (time(nullptr) < 1704067200 && uint32_t(millis()-start) < 15000) delay(100);
    if (time(nullptr) < 1704067200) return "CLOCK SYNC FAILED - RETRY";
  }
  return nullptr;
}
const char *fetch(Candidate &candidate) {
  Http http;
  int64_t length = http.open("https://uberclicker.com/flash/releases/ota.json");
  if (length < 1 || length > 8192) return "UPDATE FEED UNAVAILABLE";
  std::unique_ptr<char[]> body(new(std::nothrow) char[length]);
  if (!body) return "UPDATE MEMORY UNAVAILABLE";
  int received = 0; uint32_t start = millis();
  while (received < length) {
    if (uint32_t(millis()-start) >= 30000) return "UPDATE FEED INTERRUPTED";
    int n = esp_http_client_read(http.client, body.get()+received, int(length)-received);
    if (n <= 0) return "UPDATE FEED INTERRUPTED";
    received += n;
  }
  JsonDocument document;
  if (deserializeJson(document, body.get(), received) || !parseManifest(document.as<JsonObjectConst>(), candidate, uc::kConfigSchemaVersion, uc::kConfigProtocolVersion))
    return "NO COMPATIBLE OTA RELEASE";
  return nullptr;
}
const char *writeImage(const Candidate &candidate) {
  auto partition = esp_ota_get_next_update_partition(nullptr);
  if (!layoutMatches() || !partition || candidate.size > partition->size) return "USB OTA INSTALL REQUIRED";
  char url[192]; snprintf(url, sizeof(url), "%s%s/firmware.bin", Base, candidate.version);
  Http http;
  if (http.open(url) != candidate.size) return "IMAGE DOWNLOAD FAILED";
  esp_ota_handle_t handle;
  if (esp_ota_begin(partition, candidate.size, &handle) != ESP_OK) return "CANNOT OPEN UPDATE SLOT";
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool good = mbedtls_sha256_starts(&hash, 0) == 0;
  uint8_t buffer[4096], digest[32];
  uint32_t received = 0, start = millis(); unsigned previous = 101;
  while (good && received < candidate.size) {
    if (uint32_t(millis()-start) >= 120000) { good = false; break; }
    unsigned remaining = candidate.size-received;
    int n = esp_http_client_read(http.client, reinterpret_cast<char *>(buffer), remaining < sizeof(buffer) ? remaining : sizeof(buffer));
    good = n > 0 && mbedtls_sha256_update(&hash, buffer, n) == 0 && esp_ota_write(handle, buffer, n) == ESP_OK;
    if (good) {
      received += n;
      unsigned percent = received*100/candidate.size;
      if (percent/10 != previous/10) {
        previous = percent;
        char detail[33]; snprintf(detail, sizeof(detail), "%u%% - KEEP POWER CONNECTED", percent);
        report(State::Installing, detail, candidate.version);
      }
    }
  }
  good = good && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  char hex[65] = {};
  if (good) for (unsigned i = 0; i < 32; ++i) snprintf(hex+i*2, 3, "%02x", digest[i]);
  if (!good || strcmp(hex, candidate.sha256)) {
    esp_ota_abort(handle); return "IMAGE INCOMPLETE / BAD HASH";
  }
  if (esp_ota_end(handle) != ESP_OK) return "FIRMWARE VALIDATION FAILED";
  esp_app_desc_t description;
  if (esp_ota_get_partition_description(partition, &description) != ESP_OK ||
      strncmp(description.version, candidate.version, sizeof(description.version)) != 0 ||
      strncmp(description.project_name, "UberClicker", sizeof(description.project_name)) != 0) return "IMAGE VERSION MISMATCH";
  if (esp_ota_set_boot_partition(partition) != ESP_OK) return "CANNOT SELECT UPDATE SLOT";
  return nullptr;
}
void worker(void *) {
  Candidate candidate;
  for (;;) {
    Job job;
    if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdPASS) continue;
    const char *error = ready();
    if (job == Job::Check) {
      candidate = Candidate{};
      if (!error) error = fetch(candidate);
      if (!error) report(!strcmp(candidate.version, UBERCLICKER_VERSION) ? State::Current : State::Available,
                         "EXPERIMENTAL RELEASE", candidate.version);
    } else {
      if (!error) error = candidate.size ? writeImage(candidate) : "CHECK FOR UPDATES FIRST";
      if (!error) report(State::Restarting, "RESTARTING - KEEP POWER", candidate.version);
    }
    if (error) report(State::Failed, error);
  }
}
} // namespace
const Status &status() { return current; }
bool enabled() { return current.state != State::Off; }
bool busy() { return current.state == State::Checking || current.state == State::Installing || current.state == State::Restarting; }
void open() {
  if (enabled()) return;
  if (!jobs) {
    jobs = xQueueCreate(1, sizeof(Job)); results = xQueueCreate(1, sizeof(Status));
    if (!jobs || !results || xTaskCreate(worker, "ota-update", 16384, nullptr, 1, nullptr) != pdPASS) {
      if (jobs) vQueueDelete(jobs);
      if (results) vQueueDelete(results);
      jobs = results = nullptr;
      current.state = State::Failed; strlcpy(current.detail, "UPDATE MEMORY UNAVAILABLE", sizeof(current.detail));
      return;
    }
  }
  current = Status{}; current.state = State::Idle;
}
void close() { if (!busy()) current = Status{}; }
void check() {
  if (!enabled() || busy() || !jobs) return;
  current.state = State::Checking; strlcpy(current.detail, "CONNECTING TO UPDATE HOST", sizeof(current.detail));
  Job job = Job::Check; xQueueOverwrite(jobs, &job);
}
void install() {
  if (current.state != State::Available || !jobs) return;
  current.state = State::Installing; strlcpy(current.detail, "KEEP POWER CONNECTED", sizeof(current.detail));
  Job job = Job::Install; xQueueOverwrite(jobs, &job);
}
void poll() {
  if (results) {
    Status result;
    if (xQueueReceive(results, &result, 0) == pdPASS) {
      current = result;
      if (current.state == State::Restarting) restartAt = millis();
    }
  }
  if (current.state == State::Restarting && uint32_t(millis()-restartAt) >= 6000) ESP.restart();
}
void confirmBoot(bool healthy) {
  auto running = esp_ota_get_running_partition(); esp_ota_img_states_t state;
  if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return;
  if (healthy) esp_ota_mark_app_valid_cancel_rollback();
  else esp_ota_mark_app_invalid_rollback_and_reboot();
}
} // namespace ota_update
