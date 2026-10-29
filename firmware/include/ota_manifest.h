#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

namespace ota_update {
constexpr uint32_t SlotSize = 0x1f0000;
struct Candidate { char version[32] = {}; char sha256[65] = {}; uint32_t size = 0; };
struct Layout { uint8_t type, subtype; uint32_t offset, size; };
constexpr Layout Partitions[] = {
  {1, 2, 0x9000, 0x5000}, {1, 0, 0xe000, 0x2000},
  {0, 16, 0x10000, SlotSize}, {0, 17, 0x200000, SlotSize},
  {1, 3, 0x3f0000, 0x10000}
};
inline bool validVersion(const char *s) {
  if (!s || strlen(s) >= sizeof(Candidate::version)) return false;
  for (unsigned part = 0; part < 3; ++part) {
    const char *start = s;
    while (*s >= '0' && *s <= '9') ++s;
    if (s == start || *s++ != (part == 2 ? '-' : '.')) return false;
  }
  bool alnum = false;
  for (; *s; ++s) {
    if ((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) alnum = true;
    else if ((*s == '.' || *s == '-') && alnum) alnum = false;
    else return false;
  }
  return alnum;
}
inline bool hexDigest(const char *s) {
  if (!s || strlen(s) != 64) return false;
  for (; *s; ++s) if (!(*s >= '0' && *s <= '9') && !(*s >= 'a' && *s <= 'f')) return false;
  return true;
}
// Reject releases which would require USB migration or another protocol/schema.
inline bool parseManifest(JsonObjectConst m, Candidate &out, unsigned settingsSchema, unsigned configProtocol) {
  out = Candidate{};
  if (m["manifestVersion"] != 1 || m["board"] != "thinknode_m5" || m["chip"] != "esp32s3" ||
      m["flashSize"] != 4194304 || m["channel"] != "prerelease" ||
      !m["sourceDirty"].is<bool>() || m["sourceDirty"].as<bool>() ||
      m["settingsSchemaVersion"] != settingsSchema || m["configProtocolVersion"] != configProtocol || m["radioProtocolVersion"] != 1) return false;
  const char *version = m["version"] | "";
  if (!validVersion(version)) return false;
  auto partitions = m["partitions"].as<JsonArrayConst>();
  if (partitions.size() != sizeof(Partitions)/sizeof(Partitions[0])) return false;
  for (auto &expected : Partitions) {
    unsigned matches = 0;
    for (auto p : partitions) if (p["type"] == expected.type && p["subtype"] == expected.subtype &&
        p["offset"] == expected.offset && p["size"] == expected.size) ++matches;
    if (matches != 1) return false;
  }
  unsigned applications = 0;
  for (auto image : m["images"].as<JsonArrayConst>()) if (image["file"] == "firmware.bin") {
    if (++applications != 1 || image["offset"] != 0x10000 || !image["size"].is<uint32_t>()) return false;
    out.size = image["size"].as<uint32_t>();
    const char *hash = image["sha256"] | "";
    if (!out.size || out.size > SlotSize || !hexDigest(hash)) return false;
    strcpy(out.sha256, hash);
  }
  if (applications != 1) return false;
  strcpy(out.version, version);
  return true;
}
} // namespace ota_update
