#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
namespace host {
struct Stop {};
inline uint32_t now = 100;
inline void (*task)(void *) = nullptr;
inline std::function<void()> idle;
inline bool failAllocation = false;
}
inline uint32_t millis() { return host::now; }
inline void delay(unsigned ms) { host::now += ms; }
inline size_t strlcpy(char *out, const char *s, size_t n) { size_t len = strlen(s); if(n) { size_t count = std::min(len,n-1); memcpy(out,s,count); out[count]=0; } return len; }
struct Queue { size_t size; std::vector<uint8_t> value; };
using QueueHandle_t = Queue *;
constexpr int pdPASS = 1;
constexpr unsigned portMAX_DELAY = ~0u;
inline QueueHandle_t xQueueCreate(unsigned, size_t size) { return host::failAllocation ? nullptr : new Queue{size,{}}; }
inline void vQueueDelete(QueueHandle_t q) { delete q; }
inline int xQueueOverwrite(QueueHandle_t q,const void *p) { q->value.assign((const uint8_t *)p,(const uint8_t *)p+q->size); return pdPASS; }
inline int xQueueReceive(QueueHandle_t q,void *out,unsigned timeout) {
  if(q->value.empty() && timeout) host::idle();
  if(q->value.empty()) return 0;
  memcpy(out,q->value.data(),q->size);q->value.clear();return pdPASS;
}
inline int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,unsigned,void *) { host::task=fn;return pdPASS; }
struct Esp { unsigned restarts=0; void restart() { ++restarts; } };
inline Esp ESP;
