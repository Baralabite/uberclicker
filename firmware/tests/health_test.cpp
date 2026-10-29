#include "radio_health.h"
#include "radio_protocol.h"
#include <assert.h>
#include <limits>
#include <stdio.h>
#include <string.h>
using namespace radio_protocol;
static uint32_t randomState = 3;
static void randomBytes(void *bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    randomState = randomState * 1664525 + 1013904223;
    static_cast<uint8_t *>(bytes)[i] = randomState >> 24;
  }
}
int main() {
  uint8_t key[32];
  memset(key, 0x31, 32);
  Session tx, rx;
  tx.begin(true, key, 1, 2, randomBytes);
  rx.begin(false, key, 2, 1, randomBytes);
  ProbePacket ping, pong, forged;
  assert(tx.probe(ping));
  memcpy(forged, ping, ProbeSize);
  forged[20] ^= 1;
  assert(!rx.answerProbe(forged, ProbeSize, pong));
  assert(rx.answerProbe(ping, ProbeSize, pong));
  assert(!rx.answerProbe(
      ping, ProbeSize,
      forged)); // repeated requests do not create new response bursts
  memcpy(forged, pong, ProbeSize);
  forged[51] ^= 1;
  assert(!tx.matchProbe(forged, ProbeSize));
  assert(tx.matchProbe(pong, ProbeSize));
  assert(!tx.matchProbe(pong, ProbeSize));
  assert(tx.probe(ping));
  assert(!tx.matchProbe(pong, ProbeSize));
  assert(rx.answerProbe(ping, ProbeSize, pong));
  assert(tx.matchProbe(pong, ProbeSize));
  assert(tx.probe(ping));
  tx.invalidate(); // cue/admin pause does not cancel independent liveness
  assert(rx.answerProbe(ping, ProbeSize, pong));
  assert(tx.matchProbe(pong, ProbeSize));
  Session other;
  uint8_t otherKey[32] = {1};
  other.begin(false, otherKey, 3, 1, randomBytes);
  assert(!other.answerProbe(ping, ProbeSize, forged));
  // Each of eight outstanding receiver probes is isolated by pair key, target
  // and nonce.
  {
    Session controllers[8], receivers[8];
    ProbePacket replies[8];
    for (unsigned i = 0; i < 8; ++i) {
      uint8_t pairKey[32];
      memset(pairKey, i + 1, 32);
      controllers[i].begin(true, pairKey, 1, 100 + i, randomBytes);
      receivers[i].begin(false, pairKey, 100 + i, 1, randomBytes);
      assert(controllers[i].probe(ping));
      for (unsigned j = 0; j < i; ++j)
        assert(!receivers[j].answerProbe(ping, ProbeSize, forged));
      assert(receivers[i].answerProbe(ping, ProbeSize, replies[i]));
    }
    for (unsigned i = 0; i < 8; ++i) {
      for (unsigned j = 0; j < 8; ++j)
        if (i != j)
          assert(!controllers[j].matchProbe(replies[i], ProbeSize));
      assert(controllers[i].matchProbe(replies[i], ProbeSize));
    }
  }
  // Unsolicited/replayed challenges cannot reset the AEAD counter or restore an
  // old traffic key.
  Packet hello, challengeA, challengeB, reply;
  bool answer;
  tx.hello(hello);
  rx.receive(hello, challengeA, answer);
  tx.receive(challengeA, reply, answer);
  AdminPacket a, b;
  assert(tx.sealAdmin(Request, 1, 0, 1, reinterpret_cast<const uint8_t *>("x"),
                      1, a));
  assert(read64(a + 52) == 1);
  uint32_t epoch = tx.epoch();
  rx.begin(false, key, 2, 1, randomBytes);
  // A recorded Hello can produce a different receiver challenge after its
  // reboot.
  rx.receive(hello, challengeB, answer);
  tx.receive(challengeB, reply, answer);
  assert(tx.epoch() == epoch && !tx.accepted());
  tx.receive(challengeA, reply, answer);
  assert(tx.epoch() == epoch && !tx.accepted());
  assert(tx.probe(ping));
  assert(rx.answerProbe(ping, ProbeSize, pong));
  assert(tx.matchProbe(pong, ProbeSize));
  assert(tx.sealAdmin(Request, 2, 0, 1, reinterpret_cast<const uint8_t *>("x"),
                      1, b));
  assert(read64(b + 52) == 2 && tx.epoch() == epoch);
  tx.invalidate();
  tx.hello(hello);
  rx.receive(hello, challengeB, answer);
  tx.receive(challengeB, reply, answer);
  assert(tx.accepted() && tx.ready());
  tx.receive(challengeB, reply, answer);
  assert(!tx.accepted());
  radio_health::Observation observation;
  assert(!observation.online(0, 1000));
  observation.startProbe(0);
  assert(observation.awaitingProbe(1000, 10));
  observation.expireProbe(1021, 10);
  assert(observation.missed && !observation.online(1021, 5000));
  assert(!observation.awaitingProbe(1021, 10));
  observation.accept(-91, 7.25, 0xfffffff0);
  assert(observation.online(20, 1000) && observation.signal);
  assert(!observation.online(2000, 1000));
  observation.expire(2000, 1000, 10);
  assert(!observation.online(
      20, 1000)); // stale state cannot resurrect on a later clock wrap
  observation.startProbe(0xfffffff0);
  observation.accept(-95, 4, 10);
  observation.expireProbe(2000, 10);
  assert(!observation.missed && observation.online(2000, 5000));
  observation.accept(std::numeric_limits<float>::quiet_NaN(), 0, 3000);
  assert(observation.online(3001, 5000) && !observation.signal);
  assert(radio_health::spacing(103) == 10000 &&
         radio_health::spacing(2466) == 49320);
  assert(radio_health::freshness(8, 2466) > 8 * 49320);
  puts("radio health: fresh authenticated probes, replay/forgery rejection, no "
       "session counter reset, expiry/rollover and unknown signals passed");
}
