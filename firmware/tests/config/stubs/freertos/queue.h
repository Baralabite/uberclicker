#pragma once
#include <cstring>
#include <deque>
#include <vector>
struct TestQueue {
  size_t capacity, itemSize;
  std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = TestQueue *;
inline QueueHandle_t xQueueCreate(size_t n, size_t s) {
  return new TestQueue{n, s, {}};
}
inline int xQueueSend(QueueHandle_t q, const void *p, int) {
  if (q->items.size() >= q->capacity)
    return 0;
  q->items.emplace_back((uint8_t *)p, (uint8_t *)p + q->itemSize);
  return 1;
}
inline int xQueueReceive(QueueHandle_t q, void *p, int) {
  if (q->items.empty())
    return 0;
  memcpy(p, q->items.front().data(), q->itemSize);
  q->items.pop_front();
  return 1;
}
inline void xQueueReset(QueueHandle_t q) { q->items.clear(); }
