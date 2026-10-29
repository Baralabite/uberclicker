#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace radio_protocol {
constexpr size_t Size = 80;
using Packet = uint8_t[Size];
constexpr size_t ProbeSize = 52;
using ProbePacket = uint8_t[ProbeSize];
constexpr size_t AdminHeader = 72, AdminPayload = 160,
                 AdminMax = AdminHeader + AdminPayload + 16;
using AdminPacket = uint8_t[AdminMax];
enum AdminKind : uint8_t {
  Request = 0,
  Response = 1,
  RequestAck = 2,
  ResponseAck = 3
};
struct AdminMessage {
  uint32_t correlation = 0;
  uint16_t offset = 0, total = 0;
  uint8_t kind = 0, size = 0;
  uint8_t bytes[AdminPayload] = {};
};
enum Kind : uint8_t { Hello = 1, Challenge, Cue, Ack };
enum Result : uint8_t { Received = 1, Submitted, Rejected, Unknown, Sent };
using Random = void (*)(void *, size_t);
bool mac(const uint8_t key[32], const uint8_t *bytes, size_t size,
         uint8_t digest[32]);
uint64_t wireIdentity(const uint8_t key[32], uint64_t deviceId);
void sign(Packet &p, const uint8_t key[32]);
bool authentic(const Packet &p, const uint8_t key[32]);
uint64_t read64(const uint8_t *p);
void write64(uint8_t *p, uint64_t n);
class Session {
public:
  void begin(bool tx, const uint8_t key[32], uint64_t self, uint64_t peer,
             Random random);
  void hello(Packet &p);
  // Returns true only for a new authenticated cue; duplicates never emit again.
  bool receive(const Packet &p, Packet &reply, bool &hasReply, uint32_t now = 0,
               uint32_t airtime = 0);
  bool cue(uint8_t direction, Packet &p, uint32_t now = 0);
  void acknowledge(const Packet &cue, Result result, Packet &ack);
  bool ready() const { return ready_; }
  bool enabled() const { return keyed_; }
  uint32_t epoch() const { return adminEpoch_; }
  bool ackMatches(const Packet &p, const Packet &pending) const;
  void invalidate();
  bool accepted() const { return accepted_; }
  bool probe(ProbePacket &packet);
  bool answerProbe(const uint8_t *packet, size_t size, ProbePacket &reply);
  bool matchProbe(const uint8_t *packet, size_t size);
  size_t sealAdmin(uint8_t kind, uint32_t correlation, uint16_t offset,
                   uint16_t total, const uint8_t *bytes, size_t length,
                   AdminPacket &packet);
  // 0 rejected, 1 new, 2 exact duplicate of latest authenticated frame.
  int openAdmin(const uint8_t *packet, size_t length, AdminMessage &message);
  bool targets(const uint8_t *packet, size_t length) const;

private:
  void frame(Packet &p, Kind kind);
  bool tx_ = false, ready_ = false, keyed_ = false, accepted_ = false,
       probeWaiting_ = false, probeSeen_ = false;
  uint8_t probeNonce_[16] = {}, lastProbe_[16] = {};
  uint8_t key_[32] = {}, sender_[16] = {}, challenge_[16] = {};
  uint64_t self_ = 0, peer_ = 0, sequence_ = 0, last_ = 0;
  Result result_ = Rejected;
  uint32_t peerClock_ = 0, localClock_ = 0;
  Random random_ = nullptr;
  uint64_t adminOut_ = 0, adminIn_ = 0;
  uint32_t adminEpoch_ = 0;
  uint8_t adminTag_[16] = {};
  bool adminCrypt(bool encrypt, bool fromTx, const uint8_t *header,
                  const uint8_t *input, size_t size, uint8_t *output,
                  uint8_t *tag);
  bool probeAuth(const uint8_t *packet, size_t size, uint8_t kind) const;
  bool probeFrame(ProbePacket &packet, uint8_t kind, const uint8_t *nonce);
  void resetAdmin() {
    ++adminEpoch_;
    adminOut_ = adminIn_ = 0;
    memset(adminTag_, 0, 16);
  }
};
} // namespace radio_protocol
