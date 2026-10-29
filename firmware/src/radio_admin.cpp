#include "radio_admin.h"
#include <string.h>
namespace radio_admin {
using namespace radio_protocol;
uint32_t Transfer::budget(size_t request, size_t response) const {
  uint64_t frames = (request + AdminPayload - 1) / AdminPayload +
                    (response + AdminPayload - 1) / AdminPayload + 4;
  uint64_t milliseconds = frames * 3 * timeout_;
  return milliseconds < 3600000 ? milliseconds : 3600000;
}
void Transfer::clear() {
  session_ = nullptr;
  state_ = State::Idle;
  tx_ = false;
  waiting_ = ackPending_ = delivered_ = false;
  memset(outgoing_, 0, sizeof(outgoing_));
  memset(incoming_, 0, sizeof(incoming_));
  outSize_ = outOffset_ = inTotal_ = inUsed_ = 0;
  pendingSize_ = pendingPayload_ = 0;
  correlation_ = 0;
}
bool Transfer::begin(Session &session, uint32_t correlation, const char *json,
                     size_t size, uint32_t now, uint32_t frameMs) {
  if (busy() || !session.ready() || !correlation || !size || size > 8192)
    return false;
  clear();
  session_ = &session;
  sessionEpoch_ = session.epoch();
  tx_ = true;
  correlation_ = correlation;
  started_ = progress_ = now;
  timeout_ = 3 * frameMs + 1000;
  memcpy(outgoing_, json, size);
  outSize_ = size;
  deadline_ = budget(size, 8192);
  state_ = State::Sending;
  return true;
}
int Transfer::receive(Session &session, const uint8_t *packet, size_t size,
                      uint32_t now, uint32_t frameMs) {
  bool progress = false;
  if (session_ == &session && sessionEpoch_ != session.epoch()) {
    if (tx_) {
      abort();
      sessionEpoch_ = session.epoch();
      return false;
    }
    clear();
  }
  AdminMessage message;
  int fresh = session.openAdmin(packet, size, message);
  if (!fresh)
    return false;
  bool data = message.kind < 2;
  bool newRequest = !tx_ && message.kind == Request && message.offset == 0 &&
                    fresh == 1 && message.correlation != correlation_;
  // A new request implies the controller received the prior final response,
  // even if its ACK was lost.
  if (newRequest && (!busy() || (state_ == State::Sending && waiting_ &&
                                 outOffset_ + pendingPayload_ == outSize_))) {
    clear();
    session_ = &session;
    sessionEpoch_ = session.epoch();
    correlation_ = message.correlation;
    tx_ = false;
    state_ = State::Receiving;
    started_ = progress_ = now;
    timeout_ = 3 * frameMs + 1000;
    deadline_ = budget(message.total, 8192);
  }
  if (session_ != &session || message.correlation != correlation_)
    return true;
  if (!data) {
    uint8_t expected = tx_ ? RequestAck : ResponseAck;
    if (message.kind == expected && state_ == State::Sending && waiting_ &&
        message.total == outSize_ &&
        message.offset == outOffset_ + pendingPayload_) {
      outOffset_ = message.offset;
      waiting_ = false;
      progress_ = now;
      progress = fresh == 1;
      if (outOffset_ == outSize_) {
        state_ = tx_ ? State::AwaitResponse : State::Complete;
        delivered_ = false;
      }
    }
    return progress ? 2 : 1;
  }
  if (message.kind != (tx_ ? Response : Request))
    return true;
  if (tx_ && state_ == State::Sending && waiting_ &&
      outOffset_ + pendingPayload_ == outSize_ && message.offset == 0) {
    state_ = State::AwaitResponse;
    waiting_ = false;
  }
  if (state_ != State::Receiving && state_ != State::AwaitResponse &&
      state_ != State::RequestReady && state_ != State::Complete)
    return true;
  if (!inTotal_) {
    inTotal_ = message.total;
    uint32_t known = budget(outSize_ ? outSize_ : 8192, inTotal_);
    if (known < deadline_)
      deadline_ = known;
  }
  if (inTotal_ != message.total || message.offset > inUsed_)
    return true;
  if (message.offset < inUsed_) {
    if (message.offset + message.size > inUsed_ ||
        memcmp(incoming_ + message.offset, message.bytes, message.size))
      return true;
  } else {
    if (fresh == 2)
      return true;
    memcpy(incoming_ + inUsed_, message.bytes, message.size);
    inUsed_ += message.size;
    incoming_[inUsed_] = 0;
    progress_ = now;
    progress = true;
    if (inUsed_ == inTotal_) {
      state_ = tx_ ? State::Complete : State::RequestReady;
      delivered_ = false;
    } else
      state_ = State::Receiving;
  }
  ack_ = message;
  ack_.kind = tx_ ? ResponseAck : RequestAck;
  ack_.offset = inUsed_;
  ack_.size = 0;
  ackPending_ = true;
  return progress ? 2 : 1;
}
bool Transfer::respond(const char *json, size_t size, uint32_t now) {
  if (state_ != State::RequestReady || !size || size > 8192)
    return false;
  memcpy(outgoing_, json, size);
  outgoing_[size] = 0;
  outSize_ = size;
  outOffset_ = 0;
  waiting_ = false;
  uint32_t known = budget(inTotal_, size);
  if (known < deadline_)
    deadline_ = known;
  state_ = State::Sending;
  progress_ = now;
  return true;
}
bool Transfer::poll(uint32_t now, AdminPacket &packet, size_t &size) {
  size = 0;
  if (session_ && sessionEpoch_ != session_->epoch()) {
    if (!tx_) {
      clear();
      return false;
    }
    abort();
    sessionEpoch_ = session_->epoch();
  }
  if (!session_ || !session_->ready()) {
    if (busy())
      abort();
    return false;
  }
  if (busy() && uint32_t(now - started_) > deadline_) {
    abort();
    return false;
  }
  if (ackPending_) {
    size = session_->sealAdmin(ack_.kind, correlation_, ack_.offset, ack_.total,
                               nullptr, 0, packet);
    ackPending_ = false;
    return size != 0;
  }
  if (state_ == State::AwaitResponse || state_ == State::Receiving ||
      state_ == State::RequestReady) {
    if (uint32_t(now - progress_) > 3 * timeout_)
      abort();
    return false;
  }
  if (state_ != State::Sending)
    return false;
  if (waiting_) {
    if (uint32_t(now - sent_) < timeout_)
      return false;
    if (retries_++ >= 2) {
      abort();
      return false;
    }
    memcpy(packet, pending_, pendingSize_);
    size = pendingSize_;
    sent_ = now;
    return true;
  }
  pendingPayload_ = outSize_ - outOffset_;
  if (pendingPayload_ > AdminPayload)
    pendingPayload_ = AdminPayload;
  size = session_->sealAdmin(
      tx_ ? Request : Response, correlation_, outOffset_, outSize_,
      reinterpret_cast<const uint8_t *>(outgoing_ + outOffset_),
      pendingPayload_, packet);
  if (!size) {
    abort();
    return false;
  }
  memcpy(pending_, packet, size);
  pendingSize_ = size;
  waiting_ = true;
  retries_ = 0;
  sent_ = now;
  return true;
}
bool Transfer::takeComplete() {
  if (state_ != State::Complete || delivered_)
    return false;
  delivered_ = true;
  return true;
}
} // namespace radio_admin
