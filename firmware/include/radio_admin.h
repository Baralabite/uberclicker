#pragma once
#include "radio_protocol.h"
namespace radio_admin {
constexpr uint32_t IdleMs = 120000;
inline bool idleExpired(uint32_t now, uint32_t last) {
  return uint32_t(now - last) > IdleMs;
}
// One bounded transaction on the half-duplex radio. Peers retain independent
// sessions.
class Transfer {
public:
  enum class State {
    Idle,
    Sending,
    AwaitResponse,
    Receiving,
    RequestReady,
    Complete,
    Failed
  };
  bool begin(radio_protocol::Session &session, uint32_t correlation,
             const char *json, size_t size, uint32_t now, uint32_t frameMs);
  // 0 rejected, 1 authenticated/no progress (including replay), 2 newly
  // accepted progress.
  int receive(radio_protocol::Session &session, const uint8_t *packet,
              size_t size, uint32_t now, uint32_t frameMs);
  bool poll(uint32_t now, radio_protocol::AdminPacket &packet, size_t &size);
  bool requestReady() const { return state_ == State::RequestReady; }
  bool respond(const char *json, size_t size, uint32_t now);
  bool takeComplete();
  const char *message() const { return incoming_; }
  size_t messageSize() const { return inUsed_; }
  uint32_t correlation() const { return correlation_; }
  radio_protocol::Session *session() const { return session_; }
  bool busy() const {
    return ackPending_ || (state_ != State::Idle && state_ != State::Complete &&
                           state_ != State::Failed);
  }
  State state() const { return state_; }
  uint32_t deadlineMs() const { return deadline_; }
  void abort() {
    state_ = State::Failed;
    ackPending_ = false;
    waiting_ = false;
  }
  void clear();

private:
  radio_protocol::Session *session_ = nullptr;
  State state_ = State::Idle;
  bool tx_ = false, waiting_ = false, ackPending_ = false, delivered_ = false;
  char outgoing_[8193] = {}, incoming_[8193] = {};
  uint16_t outSize_ = 0, outOffset_ = 0, inTotal_ = 0, inUsed_ = 0;
  uint32_t correlation_ = 0, started_ = 0, progress_ = 0, sent_ = 0,
           timeout_ = 1000;
  unsigned retries_ = 0;
  uint32_t sessionEpoch_ = 0, deadline_ = 0;
  uint32_t budget(size_t request, size_t response) const;
  radio_protocol::AdminPacket pending_ = {};
  size_t pendingSize_ = 0, pendingPayload_ = 0;
  radio_protocol::AdminMessage ack_;
};
} // namespace radio_admin
