#pragma once
#include <Arduino.h>
#include <map>
#include <set>
inline std::map<std::string, String> testNvs;
inline std::set<std::string> testBlobKeys;
inline bool testWriteFailure = false, testTornWrite = false;
inline bool testReadbackFailure = false;
inline size_t testNvsOccupied = 0, testNvsPeak = 0;
inline size_t nvsCost(const String &v) {
  return ((v.length() + 31) / 32) * 32 + 64 + ((v.length() + 4031) / 4032) * 32;
}
class Preferences {
public:
  bool begin(const char *, bool) { return true; }
  void end() {}
  bool clear() {
    if (testWriteFailure)
      return false;
    testNvs.clear();
    testBlobKeys.clear();
    return true;
  }
  size_t putString(const char *k, const String &v) {
    if (testWriteFailure || v.length() >= 4000)
      return 0;
    if (testTornWrite) {
      testNvs[k] = "CORRUPTED";
      return 3;
    }
    testNvs[k] = v;
    testBlobKeys.erase(k);
    return v.length();
  }
  String getString(const char *k) {
    auto it = testNvs.find(k);
    return it == testNvs.end() || testBlobKeys.count(k) ? String() : it->second;
  }
  bool isKey(const char *k) { return testNvs.count(k); }
  bool remove(const char *k) {
    if (testWriteFailure) return false;
    testBlobKeys.erase(k);
    return testNvs.erase(k) != 0;
  }
  size_t putBytes(const char *k, const void *bytes, size_t size) {
    if (testWriteFailure) return 0;
    String value(std::string(static_cast<const char *>(bytes), size));
    size_t used = testNvsOccupied + nvsCost(value);
    for (auto &entry : testNvs) used += nvsCost(entry.second);
    // Five-page NVS: reserve one page for GC, then account for 126 entries/page.
    if (used > 4 * 126 * 32) return 0;
    testNvsPeak = std::max(testNvsPeak, used);
    testBlobKeys.insert(k);
    if (testTornWrite) { testNvs[k] = "CORRUPTED"; return 3; }
    testNvs[k] = value;
    return size;
  }
  size_t getBytesLength(const char *k) {
    auto it = testNvs.find(k);
    return it == testNvs.end() || !testBlobKeys.count(k) ? 0 : it->second.length();
  }
  size_t getBytes(const char *k, void *out, size_t size) {
    if (testReadbackFailure) return 0;
    size_t length = getBytesLength(k);
    if (size < length) return 0;
    memcpy(out, testNvs[k].c_str(), length);
    return length;
  }
};
