#include "radio_admin.h"
#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace radio_protocol;
using radio_admin::Transfer;
static uint32_t entropy = 0x12345678;
static void randomBytes(void *p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    entropy ^= entropy << 13;
    entropy ^= entropy >> 17;
    entropy ^= entropy << 5;
    static_cast<uint8_t *>(p)[i] = entropy;
  }
}
static void connect(Session &tx, Session &rx, const uint8_t *key,
                    uint64_t target) {
  tx.begin(true, key, 1, target, randomBytes);
  rx.begin(false, key, target, 1, randomBytes);
  Packet hello, challenge, reply;
  bool answer;
  tx.hello(hello);
  assert(!rx.receive(hello, challenge, answer) && answer);
  tx.receive(challenge, reply, answer);
  assert(tx.ready());
}
int main() {
  uint8_t key[32];
  memset(key, 0x73, 32);
  Session tx, rx;
  connect(tx, rx, key, 2);
  AdminPacket wrongDirection;
  assert(!rx.sealAdmin(Request, 1, 0, 1, reinterpret_cast<const uint8_t *>("x"),
                       1, wrongDirection));
  const char *secret = "{\"v\":2,\"id\":42,\"op\":\"apply\",\"password\":\"not-"
                       "cleartext-on-air\"}";
  AdminPacket packet, forged;
  size_t size = tx.sealAdmin(Request, 1, 0, strlen(secret),
                             reinterpret_cast<const uint8_t *>(secret),
                             strlen(secret), packet);
  assert(size && std::search(packet, packet + size, secret,
                             secret + strlen(secret)) == packet + size);
  AdminMessage message;
  memcpy(forged, packet, size);
  forged[AdminHeader] ^= 1;
  assert(!rx.openAdmin(forged, size, message));
  memcpy(forged, packet, size);
  forged[12] ^= 1;
  assert(!rx.openAdmin(forged, size, message));
  assert(rx.openAdmin(packet, size, message) == 1 &&
         !memcmp(message.bytes, secret, strlen(secret)));
  assert(rx.openAdmin(packet, size, message) ==
         2); // exact retransmission is identified, never a new command
  size_t later = tx.sealAdmin(
      Request, 2, 0, 1, reinterpret_cast<const uint8_t *>("x"), 1, forged);
  assert(rx.openAdmin(forged, later, message) == 1);
  assert(!rx.openAdmin(packet, size, message));
  Session reboot;
  reboot.begin(false, key, 2, 1, randomBytes);
  assert(!reboot.openAdmin(packet, size, message));
  tx.invalidate();
  Packet hello, challenge, reply;
  bool answer;
  tx.hello(hello);
  rx.receive(hello, challenge, answer);
  tx.receive(challenge, reply, answer);
  assert(!rx.openAdmin(
      packet, size,
      message)); // old session ciphertext cannot cross a fresh challenge
  // Only newly accepted progress can renew an edit lease; duplicates/replay
  // cannot.
  {
    Session a, b;
    connect(a, b, key, 9);
    Transfer lease;
    uint8_t chunk[160];
    memset(chunk, 'p', sizeof(chunk));
    size_t n = a.sealAdmin(Request, 70, 0, 320, chunk, sizeof(chunk), packet);
    assert(lease.receive(b, packet, n, 10, 8856) == 2);
    uint32_t last = 10;
    assert(lease.receive(b, packet, n, 60000, 8856) == 1);
    assert(radio_admin::idleExpired(120011, last));
    // Fresh ciphertext carrying old bytes/offset is still not forward progress.
    n = a.sealAdmin(Request, 70, 0, 320, chunk, sizeof(chunk), packet);
    assert(lease.receive(b, packet, n, 60001, 8856) == 1);
    Transfer longUpload;
    connect(a, b, key, 10);
    last = 0;
    for (unsigned i = 0; i < 10; ++i) {
      uint32_t now = i * 20000;
      n = a.sealAdmin(Request, 71, i * 160, 1600, chunk, sizeof(chunk), packet);
      assert(longUpload.receive(b, packet, n, now, 8856) == 2);
      last = now;
      AdminPacket ack;
      size_t ackSize;
      longUpload.poll(now, ack, ackSize);
      assert(!radio_admin::idleExpired(now, last));
    }
    assert(last > 120000 && longUpload.requestReady());
    Transfer limited;
    connect(a, b, key, 11);
    assert(limited.begin(a, 72, "x", 1, 0, 10));
    assert(limited.deadlineMs() < 3600000);
    Transfer larger;
    assert(larger.begin(a, 73, "longer", 6, 0, 8856));
    assert(larger.deadlineMs() <= 3600000);
  }
  // Every receiver has its own key, session, IDs, replay window and traffic
  // keys.
  Session senders[8], receivers[8];
  uint8_t keys[8][32];
  for (unsigned i = 0; i < 8; ++i) {
    memset(keys[i], i + 1, 32);
    connect(senders[i], receivers[i], keys[i], 100 + i);
  }
  for (unsigned i = 0; i < 8; ++i) {
    size = senders[i].sealAdmin(
        Request, 4, 0, 1, reinterpret_cast<const uint8_t *>("x"), 1, packet);
    for (unsigned j = 0; j < 8; ++j)
      if (j != i)
        assert(!receivers[j].openAdmin(packet, size, message));
    assert(receivers[i].openAdmin(packet, size, message) == 1);
  }
  // Fragmented request and response, one lost final request ACK and one lost
  // response ACK.
  connect(tx, rx, key, 2);
  Transfer controller, receiver;
  char request[8193], response[8193];
  memset(request, 'q', 8192);
  request[8192] = 0;
  memset(response, 's', 8192);
  response[8192] = 0;
  assert(controller.begin(tx, 17, request, 8192, 0, 10));
  assert(!controller.begin(tx, 18, request, 1, 0, 10));
  unsigned executed = 0;
  bool lostRequest = false, lostResponse = false, complete = false;
  for (uint32_t now = 0; now < 200000; now += 10) {
    if (controller.poll(now, packet, size)) {
      assert(receiver.receive(rx, packet, size, now, 10));
      // Inject an exact duplicate while the request is complete or the response
      // is in flight.
      receiver.receive(rx, packet, size, now, 10);
    }
    if (receiver.requestReady()) {
      ++executed;
      assert(receiver.messageSize() == 8192 &&
             !memcmp(receiver.message(), request, 8192));
      assert(receiver.respond(response, 8192, now));
    }
    if (receiver.poll(now, packet, size)) {
      if (packet[68] == RequestAck && packet[64] == 0x20 && !lostRequest) {
        lostRequest = true;
      } else
        assert(controller.receive(tx, packet, size, now, 10));
    }
    if (controller.state() == Transfer::State::Complete) {
      if (controller.takeComplete()) {
        complete = true;
        assert(controller.messageSize() == 8192 &&
               !memcmp(controller.message(), response, 8192));
      }
      // Drop a final response ACK, then allow response retransmission and
      // duplicate ACK recovery.
      if (!lostResponse && controller.poll(now, packet, size)) {
        assert(packet[68] == ResponseAck);
        lostResponse = true;
      }
    }
    if (complete && !controller.busy() &&
        receiver.state() == Transfer::State::Complete)
      break;
  }
  assert(complete && executed == 1 && lostRequest && lostResponse);
  assert(!controller.takeComplete());
  // Half-duplex SF12 timing model: lost final request ACK must not trigger a
  // retry in the middle of the first response frame (248-byte data=8856 ms,
  // ACK=3613 ms).
  {
    Session timedTx, timedRx;
    connect(timedTx, timedRx, key, 3);
    Transfer a, b;
    char body[320];
    memset(body, 't', sizeof(body));
    assert(a.begin(timedTx, 71, body, sizeof(body), 0, 8856));
    struct Flight {
      bool active = false, collision = false;
      uint32_t end = 0;
      size_t size = 0;
      AdminPacket bytes = {};
    } flights[2];
    unsigned effects = 0;
    bool dropped = false, finished = false;
    for (uint32_t now = 0; now < 240000; now += 10) {
      for (unsigned source = 0; source < 2; ++source) {
        auto &f = flights[source];
        if (f.active && now >= f.end) {
          bool drop = source == 1 && f.bytes[68] == RequestAck &&
                      f.bytes[64] == 1 && f.bytes[65] == 64 && !dropped;
          if (drop)
            dropped = true;
          if (!f.collision && !drop)
            (source == 0 ? b : a)
                .receive(source == 0 ? timedRx : timedTx, f.bytes, f.size, now,
                         8856);
          f.active = false;
        }
      }
      if (b.requestReady()) {
        ++effects;
        assert(b.respond(body, sizeof(body), now));
      }
      for (unsigned source = 0; source < 2; ++source) {
        auto &f = flights[source];
        if (!f.active && (source == 0 ? a : b).poll(now, f.bytes, f.size)) {
          f.active = true;
          f.collision = flights[1 - source].active;
          if (f.collision)
            flights[1 - source].collision = true;
          f.end = now + (f.size == 88 ? 3613 : 8856);
        }
      }
      if (a.takeComplete())
        finished = true;
      if (finished && b.state() == Transfer::State::Complete)
        break;
    }
    assert(dropped && finished && effects == 1);
  }
  // Same logical request ID may be reused in a NEW transport correlation
  // without replaying old bytes.
  assert(controller.begin(tx, 18, secret, strlen(secret), 210000, 10));
  for (uint32_t now = 210000; now < 215000; now += 10) {
    if (controller.poll(now, packet, size))
      receiver.receive(rx, packet, size, now, 10);
    if (receiver.requestReady()) {
      ++executed;
      assert(receiver.respond("done", 4, now));
    }
    if (receiver.poll(now, packet, size))
      controller.receive(tx, packet, size, now, 10);
    if (controller.takeComplete())
      break;
  }
  assert(executed == 2);
  // Controller reboot scopes repeated wire IDs to a fresh session; old cached
  // Apply must not run.
  tx.begin(true, key, 1, 2, randomBytes);
  tx.hello(hello);
  rx.receive(hello, challenge, answer);
  tx.receive(challenge, reply, answer);
  Transfer restarted;
  assert(restarted.begin(tx, 17, "read", 4, 220000, 10));
  for (uint32_t now = 220000; now < 225000; now += 10) {
    if (restarted.poll(now, packet, size))
      receiver.receive(rx, packet, size, now, 10);
    if (receiver.requestReady()) {
      ++executed;
      assert(receiver.messageSize() == 4 &&
             !memcmp(receiver.message(), "read", 4));
      assert(receiver.respond("revision=2", 10, now));
    }
    if (receiver.poll(now, packet, size))
      restarted.receive(tx, packet, size, now, 10);
    if (restarted.takeComplete())
      break;
  }
  assert(executed == 3);
  // Sender restart/receiver restart or silence produce failure, with no
  // whole-message replay.
  Transfer offline;
  assert(offline.begin(tx, 19, secret, strlen(secret), 0, 10));
  unsigned frames = 0;
  for (uint32_t now = 0; now < 5000; now += 100)
    if (offline.poll(now, packet, size))
      ++frames;
  assert(offline.state() == Transfer::State::Failed && frames == 3);
  Transfer interrupted;
  assert(interrupted.begin(tx, 20, secret, strlen(secret), 0, 10));
  assert(interrupted.poll(0, packet, size));
  tx.invalidate();
  assert(!interrupted.poll(1, packet, size) &&
         interrupted.state() == Transfer::State::Failed);
  // Malformed totals/offsets and wrong-direction reflected packets cannot enter
  // reassembly.
  connect(tx, rx, key, 2);
  assert(!tx.sealAdmin(Request, 1, 0, 8193,
                       reinterpret_cast<const uint8_t *>("x"), 1, packet));
  size = tx.sealAdmin(Request, 1, 0, 1, reinterpret_cast<const uint8_t *>("x"),
                      1, packet);
  assert(!tx.openAdmin(packet, size, message));
  assert(!rx.openAdmin(packet, size - 1, message));
  assert(!radio_admin::idleExpired(119999, 0));
  assert(radio_admin::idleExpired(120001, 0));
  assert(!radio_admin::idleExpired(100, 0xfffffff0));
  assert(radio_admin::idleExpired(121000, 0xfffffff0));
  puts("encrypted admin: eight-peer isolation, forgery/replay/reboot, 8KB "
       "fragmentation, lost ACK dedup, bounded failures passed");
}
