#include "radio_protocol.h"
#ifdef HOST_TEST_OPENSSL
#include <openssl/evp.h>
#include <openssl/hmac.h>
#else
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#endif
namespace radio_protocol {
bool mac(const uint8_t key[32], const uint8_t *bytes, size_t size,
         uint8_t digest[32]) {
#ifdef HOST_TEST_OPENSSL
  unsigned length = 32;
  return HMAC(EVP_sha256(), key, 32, bytes, size, digest, &length) &&
         length == 32;
#else
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 32,
                         bytes, size, digest) == 0;
#endif
}
uint64_t wireIdentity(const uint8_t key[32], uint64_t deviceId) {
  uint8_t domain[20] = {'U', 'C', '-', 'R', 'A', 'D',
                        'I', 'O', '-', 'I', 'D', 0},
          digest[32];
  write64(domain + 12, deviceId);
  return mac(key, domain, sizeof(domain), digest) ? read64(digest) : 0;
}
void sign(Packet &p, const uint8_t key[32]) {
  uint8_t d[32];
  if (mac(key, p, 64, d))
    memcpy(p + 64, d, 16);
}
bool authentic(const Packet &p, const uint8_t key[32]) {
  if (p[0] != 'U' || p[1] != 'C' || p[2] != 1 || p[3] < Hello || p[3] > Ack)
    return false;
  uint8_t d[32], diff = 0;
  if (!mac(key, p, 64, d))
    return false;
  for (unsigned i = 0; i < 16; ++i)
    diff |= d[i] ^ p[64 + i];
  return diff == 0;
}
uint64_t read64(const uint8_t *p) {
  uint64_t n = 0;
  for (unsigned i = 0; i < 8; ++i)
    n = (n << 8) | p[i];
  return n;
}
void write64(uint8_t *p, uint64_t n) {
  for (int i = 7; i >= 0; --i) {
    p[i] = n & 255;
    n >>= 8;
  }
}
void Session::begin(bool tx, const uint8_t key[32], uint64_t self,
                    uint64_t peer, Random random) {
  tx_ = tx;
  self_ = self;
  peer_ = peer;
  random_ = random;
  memcpy(key_, key, 32);
  keyed_ = self && peer && self != peer;
  uint8_t bits = 0;
  for (unsigned i = 0; i < 32; ++i)
    bits |= key[i];
  keyed_ &= bits != 0;
  ready_ = false;
  probeWaiting_ = probeSeen_ = false;
  resetAdmin();
  sequence_ = last_ = 0;
  memset(challenge_, 0, 16);
  random_(sender_, 16);
}
void Session::invalidate() {
  ready_ = false;
  resetAdmin();
  if (tx_ && random_) {
    random_(sender_, 16);
    sequence_ = 0;
  }
}
void Session::frame(Packet &p, Kind kind) {
  memset(p, 0, Size);
  p[0] = 'U';
  p[1] = 'C';
  p[2] = 1;
  p[3] = kind;
  write64(p + 4, self_);
  write64(p + 12, peer_);
  memcpy(p + 20, sender_, 16);
  memcpy(p + 36, challenge_, 16);
}
void Session::hello(Packet &p) {
  if (tx_) {
    invalidate();
    memset(challenge_, 0, sizeof(challenge_));
  }
  frame(p, Hello);
  sign(p, key_);
}
bool Session::receive(const Packet &p, Packet &reply, bool &hasReply,
                      uint32_t now, uint32_t airtime) {
  hasReply = false;
  accepted_ = false;
  if (!keyed_ || !authentic(p, key_) || read64(p + 4) != peer_ ||
      read64(p + 12) != self_)
    return false;
  if (!tx_ && p[3] == Hello) {
    if (!ready_ || memcmp(sender_, p + 20, 16)) {
      memcpy(sender_, p + 20, 16);
      random_(challenge_, 16);
      last_ = 0;
      resetAdmin();
      ready_ = true;
      accepted_ = true;
    }
    frame(reply, Challenge);
    write64(reply + 52, now);
    sign(reply, key_);
    hasReply = true;
    return false;
  }
  if (tx_ && !ready_ && p[3] == Challenge && !memcmp(sender_, p + 20, 16)) {
    if (!ready_ || memcmp(challenge_, p + 36, 16)) {
      memcpy(challenge_, p + 36, 16);
      sequence_ = 0;
      resetAdmin();
    }
    peerClock_ = uint32_t(read64(p + 52)) + airtime;
    localClock_ = now;
    ready_ = true;
    accepted_ = true;
    return false;
  }
  if (tx_ || !ready_ || p[3] != Cue || memcmp(sender_, p + 20, 16) ||
      memcmp(challenge_, p + 36, 16))
    return false;
  uint32_t stamp = (uint32_t(p[61]) << 16) | (uint32_t(p[62]) << 8) | p[63];
  int32_t age = (now - stamp) & 0xffffff;
  if (age & 0x800000)
    age -= 0x1000000;
  if (age < -250 || age > int32_t(airtime + 500))
    return false;
  uint64_t seq = read64(p + 52);
  if (!seq || seq < last_ || p[60] < 1 || p[60] > 2)
    return false;
  if (seq == last_) {
    acknowledge(p, result_, reply);
    hasReply = true;
    return false;
  }
  last_ = seq;
  result_ = Received;
  accepted_ = true;
  return true;
}
bool Session::cue(uint8_t direction, Packet &p, uint32_t now) {
  if (!tx_ || !ready_ || direction < 1 || direction > 2 ||
      sequence_ == UINT64_MAX)
    return false;
  frame(p, Cue);
  write64(p + 52, ++sequence_);
  p[60] = direction;
  uint32_t stamp = peerClock_ + uint32_t(now - localClock_);
  p[61] = stamp >> 16;
  p[62] = stamp >> 8;
  p[63] = stamp;
  sign(p, key_);
  return true;
}
void Session::acknowledge(const Packet &cue, Result result, Packet &ack) {
  if (read64(cue + 52) == last_ && !memcmp(cue + 20, sender_, 16) &&
      !memcmp(cue + 36, challenge_, 16))
    result_ = result;
  frame(ack, Ack);
  memcpy(ack + 20, cue + 20, 40);
  ack[60] = result;
  memcpy(ack + 61, cue + 61, 3);
  sign(ack, key_);
}
bool Session::ackMatches(const Packet &p, const Packet &pending) const {
  return keyed_ && authentic(p, key_) && p[3] == Ack &&
         read64(p + 4) == peer_ && read64(p + 12) == self_ &&
         !memcmp(p + 20, pending + 20, 40) &&
         !memcmp(p + 61, pending + 61, 3) && p[60] >= Received && p[60] <= Sent;
}

bool Session::probeFrame(ProbePacket &packet, uint8_t kind,
                         const uint8_t *nonce) {
  memset(packet, 0, ProbeSize);
  packet[0] = 'U';
  packet[1] = 'C';
  packet[2] = 2;
  packet[3] = kind;
  write64(packet + 4, self_);
  write64(packet + 12, peer_);
  memcpy(packet + 20, nonce, 16);
  uint8_t digest[32];
  if (!mac(key_, packet, 36, digest))
    return false;
  memcpy(packet + 36, digest, 16);
  return true;
}
bool Session::probeAuth(const uint8_t *packet, size_t size,
                        uint8_t kind) const {
  if (size != ProbeSize || !targets(packet, size) || packet[0] != 'U' ||
      packet[1] != 'C' || packet[2] != 2 || packet[3] != kind)
    return false;
  uint8_t digest[32], difference = 0;
  if (!mac(key_, packet, 36, digest))
    return false;
  for (unsigned i = 0; i < 16; ++i)
    difference |= digest[i] ^ packet[36 + i];
  return difference == 0;
}
bool Session::probe(ProbePacket &packet) {
  if (!tx_ || !keyed_)
    return false;
  random_(probeNonce_, 16);
  probeWaiting_ = probeFrame(packet, 6, probeNonce_);
  return probeWaiting_;
}
bool Session::answerProbe(const uint8_t *packet, size_t size,
                          ProbePacket &reply) {
  if (tx_ || !probeAuth(packet, size, 6) ||
      (probeSeen_ && !memcmp(lastProbe_, packet + 20, 16)))
    return false;
  if (!probeFrame(reply, 7, packet + 20))
    return false;
  memcpy(lastProbe_, packet + 20, 16);
  probeSeen_ = true;
  return true;
}
bool Session::matchProbe(const uint8_t *packet, size_t size) {
  if (!tx_ || !probeWaiting_ || !probeAuth(packet, size, 7) ||
      memcmp(probeNonce_, packet + 20, 16))
    return false;
  probeWaiting_ = false;
  return true;
}
bool Session::targets(const uint8_t *packet, size_t length) const {
  return keyed_ && length >= 20 && read64(packet + 4) == peer_ &&
         read64(packet + 12) == self_;
}
bool Session::adminCrypt(bool encrypt, bool fromTx, const uint8_t *header,
                         const uint8_t *input, size_t size, uint8_t *output,
                         uint8_t *tag) {
  uint8_t domain[45] = {'U', 'C', '-', 'A', 'D', 'M',
                        'I', 'N', '-', 'v', '2', 0},
          key[32], nonce[12] = {};
  memcpy(domain + 12, sender_, 16);
  memcpy(domain + 28, challenge_, 16);
  domain[44] = fromTx ? 1 : 2;
  if (!mac(key_, domain, sizeof(domain), key))
    return false;
  memcpy(nonce + 4, header + 52, 8);
  bool ok = false;
#ifdef HOST_TEST_OPENSSL
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return false;
  int n = 0, tail = 0;
  if (encrypt) {
    ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce) == 1 &&
         EVP_EncryptUpdate(ctx, nullptr, &n, header, AdminHeader) == 1 &&
         EVP_EncryptUpdate(ctx, output, &n, input, size) == 1 &&
         EVP_EncryptFinal_ex(ctx, output + n, &tail) == 1 &&
         EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
  } else {
    ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce) == 1 &&
         EVP_DecryptUpdate(ctx, nullptr, &n, header, AdminHeader) == 1 &&
         EVP_DecryptUpdate(ctx, output, &n, input, size) == 1 &&
         EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1 &&
         EVP_DecryptFinal_ex(ctx, output + n, &tail) == 1;
  }
  EVP_CIPHER_CTX_free(ctx);
#else
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) == 0) {
    if (encrypt)
      ok = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, size, nonce,
                                     sizeof(nonce), header, AdminHeader, input,
                                     output, 16, tag) == 0;
    else
      ok = mbedtls_gcm_auth_decrypt(&ctx, size, nonce, sizeof(nonce), header,
                                    AdminHeader, tag, 16, input, output) == 0;
  }
  mbedtls_gcm_free(&ctx);
#endif
  memset(key, 0, sizeof(key));
  if (!ok)
    memset(output, 0, size);
  return ok;
}
size_t Session::sealAdmin(uint8_t kind, uint32_t correlation, uint16_t offset,
                          uint16_t total, const uint8_t *bytes, size_t length,
                          AdminPacket &p) {
  if ((tx_ && kind != Request && kind != ResponseAck) ||
      (!tx_ && kind != Response && kind != RequestAck))
    return 0;
  if (!keyed_ || !ready_ || kind > ResponseAck || !correlation || !total ||
      total > 8192 || offset > total || length > AdminPayload ||
      adminOut_ == UINT64_MAX)
    return 0;
  if ((kind < 2 && (!length || offset + length > total)) ||
      (kind >= 2 && length))
    return 0;
  memset(p, 0, AdminMax);
  p[0] = 'U';
  p[1] = 'C';
  p[2] = 2;
  p[3] = 5;
  write64(p + 4, self_);
  write64(p + 12, peer_);
  memcpy(p + 20, sender_, 16);
  memcpy(p + 36, challenge_, 16);
  write64(p + 52, ++adminOut_);
  p[60] = correlation >> 24;
  p[61] = correlation >> 16;
  p[62] = correlation >> 8;
  p[63] = correlation;
  p[64] = offset >> 8;
  p[65] = offset;
  p[66] = total >> 8;
  p[67] = total;
  p[68] = kind;
  p[69] = length;
  if (!adminCrypt(true, tx_, p, bytes, length, p + AdminHeader,
                  p + AdminHeader + length))
    return 0;
  return AdminHeader + length + 16;
}
int Session::openAdmin(const uint8_t *p, size_t length, AdminMessage &message) {
  message = {};
  if (!ready_ || length < AdminHeader + 16 || length > AdminMax ||
      !targets(p, length) || p[0] != 'U' || p[1] != 'C' || p[2] != 2 ||
      p[3] != 5 || memcmp(p + 20, sender_, 16) ||
      memcmp(p + 36, challenge_, 16) || p[68] > ResponseAck ||
      p[69] > AdminPayload || p[70] || p[71] ||
      length != AdminHeader + p[69] + 16)
    return 0;
  if ((tx_ && p[68] != Response && p[68] != RequestAck) ||
      (!tx_ && p[68] != Request && p[68] != ResponseAck))
    return 0;
  uint64_t counter = read64(p + 52);
  if (!counter || counter < adminIn_)
    return 0;
  uint8_t tag[16];
  memcpy(tag, p + AdminHeader + p[69], 16);
  if (!adminCrypt(false, !tx_, p, p + AdminHeader, p[69], message.bytes, tag))
    return 0;
  message.correlation = (uint32_t(p[60]) << 24) | (uint32_t(p[61]) << 16) |
                        (uint32_t(p[62]) << 8) | p[63];
  message.offset = (uint16_t(p[64]) << 8) | p[65];
  message.total = (uint16_t(p[66]) << 8) | p[67];
  message.kind = p[68];
  message.size = p[69];
  if (!message.correlation || !message.total || message.total > 8192 ||
      message.offset > message.total ||
      (message.kind < 2 &&
       (!message.size || message.offset + message.size > message.total)) ||
      (message.kind >= 2 && message.size))
    return 0;
  if (counter == adminIn_)
    return !memcmp(tag, adminTag_, 16) ? 2 : 0;
  adminIn_ = counter;
  memcpy(adminTag_, tag, 16);
  return 1;
}
} // namespace radio_protocol
