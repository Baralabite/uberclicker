#pragma once
#include <ArduinoJson.h>
#include "output_safety.h"

namespace outputs {
enum class Result : uint8_t { Rejected, Pending, Submitted, Sent, Unknown };
// One bounded event at a time; never retry a cue after a pause or reconnect.
class ActionSequence {
  JsonDocument actions;
  size_t index = 0;
  uint32_t started = 0, epoch = 0;
  bool running = false, waiting = false, available = false;
  bool failed = false, uncertain = false, submitted = false, sent = false;
  Result result = Result::Rejected;
  void record(Result value) {
    failed |= value == Result::Rejected;
    uncertain |= value == Result::Unknown;
    submitted |= value == Result::Submitted;
    sent |= value == Result::Sent;
  }
  void finish() {
    // A partial failure must never be hidden by a later successful action.
    result = uncertain || (failed && (sent || submitted)) ? Result::Unknown :
             failed ? Result::Rejected : submitted ? Result::Submitted : Result::Sent;
    running = false; available = true; actions.clear();
  }
public:
  uint32_t issuedAt() const { return started; }
  bool busy() const { return running; }
  Result start(JsonArrayConst list, uint32_t now, uint32_t generation) {
    if (running || !list || list.size() > 8) return Result::Rejected;
    actions.clear(); auto enabled = actions.to<JsonArray>();
    for (auto action : list) if (action["enabled"] == true) enabled.add(action);
    index = 0; started = now; epoch = generation;
    waiting = available = failed = uncertain = submitted = sent = false;
    if (actions.overflowed()) { actions.clear(); return Result::Rejected; }
    running = true;
    return Result::Pending;
  }
  void complete(Result value) {
    if (!running || !waiting) return;
    waiting = false; record(value); ++index;
  }
  template<class Submit, class Ready>
  void poll(uint32_t now, uint32_t generation, bool paused, Submit submit, Ready ready) {
    if (!running) return;
    if (!output_safety::current(paused, epoch, generation, started, now)) {
      record(waiting ? Result::Unknown : Result::Rejected); finish(); return;
    }
    if (waiting) return;
    if (index == actions.size()) { finish(); return; }
    JsonObjectConst action = actions[index];
    if (!ready(action)) return;
    Result value = submit(action);
    if (value == Result::Pending) waiting = true;
    else { record(value); ++index; }
  }
  bool takeResult(Result &value) {
    if (!available) return false;
    available = false; value = result; return true;
  }
};
}
