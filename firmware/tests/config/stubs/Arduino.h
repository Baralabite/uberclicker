#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
using std::min;
class String {
  std::string s;

public:
  String() = default;
  String(const char *v) : s(v ? v : "") {}
  String(const std::string &v) : s(v) {}
  String(char c) : s(1, c) {}
  size_t length() const { return s.size(); }
  const char *c_str() const { return s.c_str(); }
  bool isEmpty() const { return s.empty(); }
  bool concat(const char *p) {
    s += p;
    return true;
  }
  void reserve(size_t n) { s.reserve(n); }
  char operator[](size_t n) const { return s[n]; }
  String &operator+=(const String &p) {
    s += p.s;
    return *this;
  }
  String &operator+=(char c) {
    s += c;
    return *this;
  }
  String &operator=(const char *p) {
    s = p ? p : "";
    return *this;
  }
  friend String operator+(String a, const String &b) {
    a += b;
    return a;
  }
  friend bool operator==(const String &a, const String &b) {
    return a.s == b.s;
  }
  friend bool operator!=(const String &a, const String &b) { return !(a == b); }
  auto begin() const { return s.begin(); }
  auto end() const { return s.end(); }
};
inline uint32_t testMillis = 0;
inline uint32_t millis() { return testMillis; }
