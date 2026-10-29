#include "config_service.h"
#include "device_ui.h"
#include "ota_update.h"
#include "outputs.h"
#include "radio_admin.h"
#include "radio_health.h"
#include "radio_protocol.h"
#include "reset_journal.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_random.h>

extern "C" bool verifyRollbackLater() { return true; }
using namespace radio_protocol;
static SX1262 radio = new Module(17, 4, 6, 5);
struct Peer {
  bool used = false, adminAuthorized = false;
  char id[13] = {}, keyHex[65] = {};
  Session session;
  uint32_t helloAt = 0, sessionAt = 0, adminAt = 0;
  radio_health::Observation health;
};
static Peer peers[8];
static bool healthBusy() {
  for (auto &peer : peers)
    if (peer.used && peer.health.probePending)
      return true;
  return false;
}
static Peer *cuePeer = nullptr, *pendingPeer = nullptr, *outputPeer = nullptr;
static radio_admin::Transfer admin;
static String bridgeReceiver, bridgeRequest, rxEditSession, rxEditController;
static uint32_t bridgeCorrelation = 0, bridgeAt = 0, wireCounter = 0,
                cancelId = 0x40000000;
static bool bridgeQueued = false, cancelling = false, bridgeNegotiated = false;
static uint32_t adminAirtime = 0, transmitAirtime = 0, healthAirtime = 0,
                lastProbeAt = 0, lastProbeReply = 0;
static unsigned nextProbe = 0;
static bool sentProbeReply = false;
static uint8_t pendingAckSeen = 0;
static uint8_t radioSf = 0;
static device_ui::Controller ui;
static device_ui::Snapshot snapshot;
static device_ui::RemoteStatus remoteDisplay;
static uint32_t remoteEditTouched = 0;
static bool radioReady = false, paired = false, tx = true, direct = false,
            transmitting = false;
static bool awaiting = false, remoteOutput = false, deferredAck = false;
static volatile bool irq = false;
static uint32_t airtime = 0, sentAt = 0, pendingAt = 0;
static Packet pending{}, receivedCue{}, ackLater{};
static uint64_t deviceId = 0;
static unsigned profileIndex = 0;
static bool configPaused = true, lastPaused = true;
static bool reconfigure = false, updateScreenRequested = false;
static bool expander(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(0x18);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}
static void IRAM_ATTR radioInterrupt() { irq = true; }
static void randomBytes(void *data, size_t n) { esp_fill_random(data, n); }
static uint64_t identity(const char *hex) { return strtoull(hex, nullptr, 16); }
static device_ui::Outcome outcome(outputs::Result result) {
  switch (result) {
  case outputs::Result::Pending:
    return device_ui::Outcome::Pending;
  case outputs::Result::Submitted:
    return device_ui::Outcome::Submitted;
  case outputs::Result::Sent:
    return device_ui::Outcome::Sent;
  case outputs::Result::Unknown:
    return device_ui::Outcome::Unknown;
  default:
    return device_ui::Outcome::Rejected;
  }
}
static Result radioResult(outputs::Result result) {
  switch (result) {
  case outputs::Result::Pending:
    return Received;
  case outputs::Result::Submitted:
    return Submitted;
  case outputs::Result::Sent:
    return Sent;
  case outputs::Result::Unknown:
    return Unknown;
  default:
    return Rejected;
  }
}
static void receiveRadio() {
  if (radioReady)
    radio.startReceive();
}
static bool sendBytes(const uint8_t *packet, size_t size) {
  if (!radioReady || transmitting)
    return false;
  radio.standby();
  irq = false;
  transmitAirtime = (radio.getTimeOnAir(size) + 999) / 1000;
  transmitting = radio.startTransmit(packet, size) == RADIOLIB_ERR_NONE;
  sentAt = millis();
  if (!transmitting)
    receiveRadio();
  return transmitting;
}
static bool send(const Packet &packet) { return sendBytes(packet, Size); }
static Peer *findPeer(const String &id) {
  for (auto &peer : peers)
    if (peer.used && id == peer.id)
      return &peer;
  return nullptr;
}
static void startPeer(Peer &peer) {
  uint8_t key[32];
  String hex = peer.keyHex;
  for (unsigned i = 0; i < 32; ++i)
    key[i] = strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  peer.health.probePending = false;
  peer.session.begin(tx, key, wireIdentity(key, deviceId),
                     wireIdentity(key, identity(peer.id)), randomBytes);
  memset(key, 0, sizeof(key));
  peer.helloAt = millis() - (2 * airtime + 1000);
  peer.sessionAt = peer.adminAt = millis();
}
static void reloadPeers() {
  const auto &settings = uc::configService.settings();
  auto configured = settings["peers"].as<JsonArrayConst>();
  for (auto &peer : peers)
    if (peer.used) {
      bool retained = false;
      for (auto entry : configured)
        if (entry["deviceId"] == peer.id)
          retained = true;
      if (!retained) {
        if (admin.session() == &peer.session)
          admin.abort();
        peer = Peer{};
      }
    }
  for (auto entry : configured) {
    String id = entry["deviceId"].as<String>(),
           hex = uc::configService.pairKeyHex(id);
    Peer *peer = findPeer(id);
    if (!peer)
      for (auto &slot : peers)
        if (!slot.used) {
          peer = &slot;
          break;
        }
    if (!peer || hex.length() != 64)
      continue;
    bool granted = entry["adminAuthorized"] | false;
    bool changed = !peer->used || strcmp(peer->keyHex, hex.c_str()) ||
                   peer->adminAuthorized != granted;
    peer->used = true;
    peer->adminAuthorized = granted;
    strlcpy(peer->id, id.c_str(), sizeof(peer->id));
    if (changed) {
      if (admin.session() == &peer->session)
        admin.abort();
      strlcpy(peer->keyHex, hex.c_str(), sizeof(peer->keyHex));
      peer->health = radio_health::Observation{};
      startPeer(*peer);
    }
  }
  unsigned active = settings["activePeer"] | 0;
  cuePeer = active < configured.size()
                ? findPeer(configured[active]["deviceId"].as<String>())
                : nullptr;
  paired = cuePeer && cuePeer->session.enabled();
}
static JsonObjectConst profile() {
  return uc::configService.settings()["profiles"][profileIndex]
      .as<JsonObjectConst>();
}
static void pauseOutput(bool value) {
  configPaused = value;
  outputs::pause(value || ui.cuesPaused());
}
static void changed() { reconfigure = true; }
static void applySettings() {
  outputs::pause(true);
  awaiting = false;
  remoteOutput = false;
  deferredAck = false;
  auto &settings = uc::configService.settings();
  bool wasTx = tx;
  tx = !strcmp(settings["role"] | "tx", "tx");
  direct = !strcmp(settings["route"] | "lora", "direct");
  profileIndex = settings["activeProfile"] | 0;
  paired = false;
  if (wasTx != tx) {
    admin.abort();
    for (auto &peer : peers)
      peer = Peer{};
  }
  reloadPeers();
  outputs::configureEndpoint(profile()["host"] | "");
  outputs::configureWifi(settings["wifi"]["ssid"] | "",
                         uc::configService.wifiPassword().c_str());
  uint8_t sf = !strcmp(settings["radio"] | "balanced", "long_range") ? 12 : 7;
  if (!radioReady || radioSf != sf) {
    if (radioSf && radioSf != sf) {
      admin.abort();
      for (auto &peer : peers)
        if (peer.used) {
          peer.session.invalidate();
          peer.health = radio_health::Observation{};
        }
    }
    if (transmitting) {
      radio.finishTransmit();
      transmitting = false;
    }
    if (radioReady)
      radio.standby();
    radioReady =
        radio.begin(917.0, 125.0, sf, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 10,
                    8, 3.3) == RADIOLIB_ERR_NONE;
    if (radioReady)
      radioReady = radio.setDio2AsRfSwitch(true) == RADIOLIB_ERR_NONE &&
                   radio.setCRC(2) == RADIOLIB_ERR_NONE &&
                   radio.setCurrentLimit(140) == RADIOLIB_ERR_NONE &&
                   radio.setRxBoostedGainMode(true) == RADIOLIB_ERR_NONE;
    if (radioReady) {
      radioSf = sf;
      airtime = (radio.getTimeOnAir(Size) + 999) / 1000;
      adminAirtime = (radio.getTimeOnAir(AdminMax) + 999) / 1000;
      healthAirtime = (radio.getTimeOnAir(ProbeSize) + 999) / 1000;
      lastProbeAt = millis() - radio_health::spacing(healthAirtime);
      for (auto &peer : peers)
        if (peer.used && !peer.session.ready())
          peer.helloAt = millis() - (2 * airtime + 1000);
      radio.setDio1Action(radioInterrupt);
      receiveRadio();
    }
  }
  uc::configService.setRemoteTransportAvailable(
      radioReady && !uc::configService.status().recoveryRequired);
  reconfigure = false;
  Serial.printf("Radio: %s; cue %lu ms, admin max %lu ms\n",
                radioReady ? "ready" : "failed", (unsigned long)airtime,
                (unsigned long)adminAirtime);
  outputs::pause(configPaused || ui.cuesPaused());
}
static void statusChanged(const uc::ConfigStatus &s) {
  outputs::securityPasskey(s.passkey);
  outputs::setupWindow(s.setup);
}
static void updateSnapshot() {
  auto status = uc::configService.status();
  snapshot.role = tx ? device_ui::Role::Tx : device_ui::Role::Rx;
  snapshot.route = direct ? device_ui::Route::Direct : device_ui::Route::LoRa;
  snapshot.paired = paired;
  snapshot.setup = status.remoteEditing ? device_ui::Setup::RemoteEditing
                   : status.editing     ? device_ui::Setup::Editing
                   : status.setup ? (status.claimed ? device_ui::Setup::App
                                                    : device_ui::Setup::Claim)
                                  : device_ui::Setup::None;
  snapshot.paused = configPaused;
  snapshot.recoveryRequired = status.recoveryRequired;
  snapshot.update = ota_update::status();
  snapshot.remote = remoteDisplay;
  strlcpy(snapshot.cueReceiverId, cuePeer ? cuePeer->id : "",
          sizeof(snapshot.cueReceiverId));
  if (!tx) {
    snapshot.remote.state = device_ui::RemoteState::None;
    snapshot.remote.editing = status.remoteEditing;
    strlcpy(snapshot.remote.deviceId, rxEditController.c_str(),
            sizeof(snapshot.remote.deviceId));
    for (auto entry :
         uc::configService.settings()["peers"].as<JsonArrayConst>())
      if (entry["deviceId"] == rxEditController)
        strlcpy(snapshot.remote.name, entry["name"] | "Controller",
                sizeof(snapshot.remote.name));
  }
  snapshot.receiverRosterKnown = !status.recoveryRequired;
  snapshot.linkMonitoringAvailable = tx && radioReady && !ota_update::enabled();
  unsigned count = uc::configService.settings()["peers"].size(), rowIndex = 0;
  uint32_t freshFor = radio_health::freshness(count, healthAirtime);
  for (auto &row : snapshot.receivers)
    row = device_ui::ReceiverLink{};
  if (tx)
    for (auto entry :
         uc::configService.settings()["peers"].as<JsonArrayConst>()) {
      if (rowIndex >= 8)
        break;
      auto &row = snapshot.receivers[rowIndex++];
      row.paired = true;
      String id = entry["deviceId"].as<String>();
      strlcpy(row.deviceId, id.c_str(), sizeof(row.deviceId));
      strlcpy(row.name, entry["name"] | "Receiver", sizeof(row.name));
      Peer *peer = findPeer(id);
      if (!peer)
        continue;
      row.fresh = snapshot.linkMonitoringAvailable &&
                  peer->health.online(millis(), freshFor);
      row.state = row.fresh ? device_ui::ReceiverState::Connected
                  : (peer->health.known || peer->health.missed)
                      ? device_ui::ReceiverState::Offline
                      : device_ui::ReceiverState::Unknown;
      row.signalValid = row.fresh && peer->health.signal;
      row.rssiDbm =
          peer->health.signal ? int16_t(lroundf(peer->health.rssi)) : 0;
    }
  auto selected = profile();
  strlcpy(snapshot.profile, selected["name"] | "Presenter",
          sizeof(snapshot.profile));
  strlcpy(snapshot.output, selected["output"] | "ble_keyboard",
          sizeof(snapshot.output));
  strlcpy(snapshot.previous, "Previous", sizeof(snapshot.previous));
  strlcpy(snapshot.next, "Next", sizeof(snapshot.next));
  snprintf(snapshot.deviceName, sizeof(snapshot.deviceName), "UberClicker %04x",
           unsigned(deviceId & 65535));
  if (status.setup) {
    strlcpy(snapshot.setupLine1, "BLE passkey / claim code",
            sizeof(snapshot.setupLine1));
    snprintf(snapshot.setupLine2, sizeof(snapshot.setupLine2), "%06lu",
             (unsigned long)status.passkey);
  } else
    snapshot.setupLine2[0] = 0;
  if (status.recoveryRequired) {
    snapshot.setup = device_ui::Setup::App;
    strlcpy(snapshot.setupLine1, "Settings need recovery",
            sizeof(snapshot.setupLine1));
    strlcpy(snapshot.setupLine2, "Both: recovery menu",
            sizeof(snapshot.setupLine2));
  }
  if (status.remotePending || status.remoteEditing) {
    snapshot.setup =
        tx ? device_ui::Setup::App : device_ui::Setup::RemoteEditing;
    strlcpy(snapshot.setupLine1, tx ? "Remote receiver" : "Controlled by Tx",
            sizeof(snapshot.setupLine1));
    strlcpy(snapshot.setupLine2,
            tx ? status.remoteDeviceId.c_str() : rxEditController.c_str(),
            sizeof(snapshot.setupLine2));
  }
  static uint32_t batteryAt = 0;
  if (!batteryAt || uint32_t(millis() - batteryAt) > 60000) {
    batteryAt = millis();
    int mv = analogReadMilliVolts(8) * 2;
    snapshot.battery = mv < 2500   ? device_ui::Battery::Unknown
                       : mv < 3500 ? device_ui::Battery::Low
                       : mv < 3900 ? device_ui::Battery::Medium
                                   : device_ui::Battery::High;
  }
  ui.setSnapshot(snapshot);
  device_ui::Frame frame;
  ui.render(frame);
  device_ui::submitDisplay(frame);
}
static bool paused() {
  return ota_update::enabled() || configPaused ||
         uc::configService.outputPaused() || ui.cuesPaused();
}
static void action(bool next) {
  if (!tx || paused()) {
    snapshot.outcome = device_ui::Outcome::Rejected;
    return;
  }
  strlcpy(snapshot.lastAction, next ? "Next" : "Previous",
          sizeof(snapshot.lastAction));
  if (direct) {
    if (remoteOutput) {
      snapshot.outcome = device_ui::Outcome::Rejected;
      return;
    }
    snapshot.outcome = outcome(outputs::submit(profile(), next));
    return;
  }
  if (!paired || !radioReady || awaiting || transmitting || healthBusy() ||
      !cuePeer->session.cue(next ? 2 : 1, pending, millis())) {
    snapshot.outcome = device_ui::Outcome::Rejected;
    return;
  }
  pendingPeer = cuePeer;
  pendingAckSeen = 0;
  awaiting = send(pending);
  pendingAt = millis();
  snapshot.outcome =
      awaiting ? device_ui::Outcome::Pending : device_ui::Outcome::Unknown;
}
static void pollRadio() {
  if (!radioReady)
    return;
  if (transmitting) {
    bool done = irq;
    if (!done && uint32_t(millis() - sentAt) < transmitAirtime + 200)
      return;
    int state = radio.finishTransmit();
    transmitting = false;
    irq = false;
    receiveRadio();
    if ((!done || state != RADIOLIB_ERR_NONE) && awaiting) {
      awaiting = false;
      snapshot.outcome = device_ui::Outcome::Unknown;
    }
    return;
  }
  if (deferredAck) {
    if (send(ackLater))
      deferredAck = false;
    return;
  }
  if (irq) {
    irq = false;
    AdminPacket bytes{};
    Packet p{}, reply{};
    bool answer = false;
    size_t size = radio.getPacketLength();
    int state =
        size <= AdminMax ? radio.readData(bytes, size) : RADIOLIB_ERR_UNKNOWN;
    float rssi = radio.getRSSI(), snr = radio.getSNR();
    receiveRadio();
    if (state != RADIOLIB_ERR_NONE)
      return;
    Peer *peer = nullptr;
    for (auto &candidate : peers)
      if (candidate.used && candidate.session.targets(bytes, size)) {
        peer = &candidate;
        break;
      }
    if (!peer)
      return;
    if (size == ProbeSize && bytes[2] == 2) {
      if (tx && peer->health.awaitingProbe(millis(), healthAirtime) &&
          peer->session.matchProbe(bytes, size)) {
        peer->health.accept(rssi, snr, millis());
        peer->health.probePending = false;
      } else if (!tx && !admin.busy() && !remoteOutput && !deferredAck &&
                 !ota_update::enabled() &&
                 (!sentProbeReply ||
                  uint32_t(millis() - lastProbeReply) >=
                      radio_health::replySpacing(healthAirtime))) {
        ProbePacket pong;
        if (peer->session.answerProbe(bytes, size, pong) &&
            sendBytes(pong, ProbeSize)) {
          lastProbeReply = millis();
          sentProbeReply = true;
        }
      }
      return;
    }
    if (size >= AdminHeader + 16 && bytes[2] == 2 && bytes[3] == 5) {
      if (!ota_update::enabled() && peer->adminAuthorized &&
          admin.receive(peer->session, bytes, size, millis(), adminAirtime) ==
              2) {
        bool radioJob =
            !tx || (bridgeQueued &&
                    (cancelling ||
                     uc::configService.remoteRequestActive(bridgeCorrelation)));
        if (radioJob) {
          peer->adminAt = millis();
          peer->health.accept(rssi, snr, millis());
        }
        bool activeJob =
            !tx || (bridgeQueued && !cancelling &&
                    uc::configService.remoteRequestActive(bridgeCorrelation));
        if (activeJob)
          uc::configService.noteRemoteProgress(peer->id);
        if (tx && activeJob && remoteDisplay.editing &&
            !strcmp(peer->id, remoteDisplay.deviceId))
          remoteEditTouched = millis();
      }
      return;
    }
    if (size != Size)
      return;
    memcpy(p, bytes, Size);
    if (tx && awaiting &&
        uint32_t(millis() - pendingAt) <= 3 * airtime + 1000 &&
        pendingPeer == peer && peer->session.ackMatches(p, pending)) {
      if (pendingAckSeen != p[60]) {
        pendingAckSeen = p[60];
        peer->health.accept(rssi, snr, millis());
      }
      switch (p[60]) {
      case Received:
        snapshot.outcome = device_ui::Outcome::Received;
        break;
      case Submitted:
        snapshot.outcome = device_ui::Outcome::Submitted;
        break;
      case Sent:
        snapshot.outcome = device_ui::Outcome::Sent;
        break;
      case Rejected:
        snapshot.outcome = device_ui::Outcome::Rejected;
        break;
      default:
        snapshot.outcome = device_ui::Outcome::Unknown;
      }
      if (p[60] != Received)
        awaiting = false;
    }
    bool cue = peer->session.receive(p, reply, answer, millis(), airtime);
    if (peer->session.accepted() && (cue || tx))
      peer->health.accept(rssi, snr, millis());
    if (cue) {
      outputs::Result result = paused() || remoteOutput
                                   ? outputs::Result::Rejected
                                   : outputs::submit(profile(), p[60] == 2);
      snapshot.outcome = result == outputs::Result::Pending
                             ? device_ui::Outcome::Received
                             : outcome(result);
      strlcpy(snapshot.lastAction, p[60] == 2 ? "Next" : "Previous",
              sizeof(snapshot.lastAction));
      peer->session.acknowledge(p, radioResult(result), reply);
      answer = true;
      if (result == outputs::Result::Pending) {
        remoteOutput = true;
        outputPeer = peer;
        memcpy(receivedCue, p, Size);
      }
    }
    if (answer) {
      if (reply[3] == Challenge)
        peer->sessionAt = millis();
      send(reply);
    }
  }
}
static String remoteError(uint32_t id, const char *code) {
  JsonDocument response;
  response["v"] = uc::kConfigProtocolVersion;
  response["id"] = id;
  response["ok"] = false;
  response["error"]["code"] = code;
  response["error"]["message"] =
      !strcmp(code, "REMOTE_TIMEOUT")
          ? "Outcome unknown; read receiver revision before applying again."
          : "Receiver unavailable; read revision if a prior Apply was "
            "interrupted.";
  String json;
  serializeJson(response, json);
  return json;
}
static void pendingDisplay() {
  using namespace device_ui;
  if (strcmp(remoteDisplay.deviceId, bridgeReceiver.c_str()))
    remoteDisplay = RemoteStatus{};
  remoteDisplay.state = RemoteState::Pending;
  remoteDisplay.resultApplied = false;
  strlcpy(remoteDisplay.deviceId, bridgeReceiver.c_str(),
          sizeof(remoteDisplay.deviceId));
  for (auto entry : uc::configService.settings()["peers"].as<JsonArrayConst>())
    if (entry["deviceId"] == bridgeReceiver)
      strlcpy(remoteDisplay.name, entry["name"] | "Receiver",
              sizeof(remoteDisplay.name));
  JsonDocument request;
  deserializeJson(request, bridgeRequest);
  const char *op = request["op"] | "";
  remoteDisplay.operation = !strcmp(op, "read")    ? RemoteOperation::Read
                            : !strcmp(op, "begin") ? RemoteOperation::Begin
                            : !strcmp(op, "validate")
                                ? RemoteOperation::Validate
                            : !strcmp(op, "apply")  ? RemoteOperation::Apply
                            : !strcmp(op, "cancel") ? RemoteOperation::Cancel
                            : !strcmp(op, "result") ? RemoteOperation::Result
                                                    : RemoteOperation::Status;
}
static void finishBridge(const String &response) {
  using namespace device_ui;
  JsonDocument decoded;
  bool valid = !deserializeJson(decoded, response) &&
               decoded["v"] == uc::kConfigProtocolVersion &&
               decoded["id"] == bridgeCorrelation && decoded["ok"].is<bool>();
  if (cancelling) {
    if (valid && decoded["ok"] == true &&
        !strcmp(remoteDisplay.deviceId, bridgeReceiver.c_str()))
      remoteDisplay.editing = false;
  } else {
    if (!valid)
      remoteDisplay.state = RemoteState::Unknown;
    else if (decoded["ok"] == true) {
      remoteDisplay.state = RemoteState::Confirmed;
      if (remoteDisplay.operation == RemoteOperation::Begin) {
        remoteDisplay.editing =
            decoded["result"]["session"].as<String>().length() == 16;
        remoteEditTouched = millis();
        if (!remoteDisplay.editing)
          remoteDisplay.state = RemoteState::Unknown;
      }
      if (remoteDisplay.operation == RemoteOperation::Apply ||
          remoteDisplay.operation == RemoteOperation::Cancel)
        remoteDisplay.editing = false;
      if (remoteDisplay.operation == RemoteOperation::Status &&
          decoded["result"]["remoteEditing"].is<bool>())
        remoteDisplay.editing = decoded["result"]["remoteEditing"];
      if (remoteDisplay.operation == RemoteOperation::Apply ||
          remoteDisplay.operation == RemoteOperation::Result) {
        JsonDocument request;
        deserializeJson(request, bridgeRequest);
        String transaction = request["transactionId"].as<String>();
        bool match =
            transaction.length() == 32 &&
            decoded["result"]["transactionId"].as<String>() == transaction;
        remoteDisplay.resultApplied =
            match && decoded["result"]["state"] == "applied";
        if (!match)
          remoteDisplay.state = RemoteState::Unknown;
      }
    } else {
      const char *code = decoded["error"]["code"] | "";
      remoteDisplay.state =
          !strcmp(code, "REMOTE_TIMEOUT")       ? RemoteState::Unknown
          : !strcmp(code, "REMOTE_UNAVAILABLE") ? RemoteState::Unreachable
                                                : RemoteState::Rejected;
      if (!strcmp(code, "SESSION_EXPIRED"))
        remoteDisplay.editing = false;
    }
    uc::configService.completeRemoteRequest(bridgeCorrelation, response);
  }
  bridgeQueued = false;
  cancelling = false;
  bridgeNegotiated = false;
  bridgeRequest = "";
  bridgeReceiver = "";
}
static void pollAdmin() {
  uint32_t now = millis();
  if (tx && remoteDisplay.editing &&
      radio_admin::idleExpired(now, remoteEditTouched)) {
    remoteDisplay.editing = false;
    remoteDisplay.state = device_ui::RemoteState::Unknown;
  }
  if (!tx) {
    if (admin.requestReady() && ota_update::enabled())
      admin.abort();
    if (admin.requestReady()) {
      Peer *controller = nullptr;
      for (auto &peer : peers)
        if (peer.used && admin.session() == &peer.session)
          controller = &peer;
      if (!controller) {
        admin.abort();
        return;
      }
      String response = uc::configService.processRemoteRequest(
          controller->id, String(admin.message(), admin.messageSize()));
      JsonDocument decoded;
      if (!deserializeJson(decoded, response) && decoded["ok"] == true &&
          decoded["result"]["session"].is<const char *>()) {
        rxEditSession = decoded["result"]["session"].as<String>();
        rxEditController = controller->id;
      }
      controller->adminAt = now;
      if (!admin.respond(response.c_str(), response.length(), now))
        admin.abort();
    }
    auto status = uc::configService.status();
    if (!status.remoteEditing) {
      rxEditSession = "";
      rxEditController = "";
    }
    // Authenticated fragments/ACKs refresh liveness; an abandoned edit never
    // holds output for an hour.
    Peer *controller = findPeer(rxEditController);
    if (status.remoteEditing && controller &&
        radio_admin::idleExpired(now, controller->adminAt)) {
      JsonDocument cancel;
      cancel["v"] = uc::kConfigProtocolVersion;
      cancel["id"] = 1;
      cancel["op"] = "cancel";
      cancel["session"] = rxEditSession;
      String json;
      serializeJson(cancel, json);
      uc::configService.processRemoteRequest(controller->id, json);
      admin.abort();
    }
  } else {
    if (ota_update::enabled()) {
      if (bridgeQueued)
        finishBridge(remoteError(bridgeCorrelation, "REMOTE_TIMEOUT"));
      admin.abort();
      return;
    }
    String target, session;
    if (uc::configService.takeRemoteCancellation(target, session)) {
      if (remoteDisplay.state == device_ui::RemoteState::Pending)
        remoteDisplay.state = device_ui::RemoteState::Unknown;
      admin.clear();
      bridgeReceiver = target;
      bridgeCorrelation = ++cancelId;
      bridgeAt = now;
      bridgeQueued = true;
      cancelling = true;
      bridgeNegotiated = false;
      JsonDocument request;
      request["v"] = uc::kConfigProtocolVersion;
      request["id"] = bridgeCorrelation;
      request["op"] = "cancel";
      request["session"] = session;
      bridgeRequest = "";
      serializeJson(request, bridgeRequest);
    }
    if (bridgeQueued && !cancelling &&
        !uc::configService.remoteRequestActive(bridgeCorrelation)) {
      Peer *old = findPeer(bridgeReceiver);
      if (old) {
        old->session.invalidate();
      }
      admin.clear();
      bridgeQueued = false;
      bridgeNegotiated = false;
      bridgeRequest = "";
      remoteDisplay.state = device_ui::RemoteState::Unknown;
    }
    if (!bridgeQueued && !admin.busy() &&
        uc::configService.takeRemoteRequest(target, bridgeRequest,
                                            bridgeCorrelation)) {
      admin.clear();
      bridgeReceiver = target;
      bridgeAt = now;
      bridgeQueued = true;
      cancelling = false;
      bridgeNegotiated = false;
      pendingDisplay();
    }
    if (bridgeQueued) {
      if (healthBusy())
        return;
      Peer *peer = findPeer(bridgeReceiver);
      if (!peer || !peer->adminAuthorized || !radioReady) {
        finishBridge(remoteError(bridgeCorrelation, "REMOTE_UNAVAILABLE"));
        admin.clear();
      } else if (admin.state() == radio_admin::Transfer::State::Idle) {
        if (!bridgeNegotiated) {
          startPeer(*peer);
          bridgeNegotiated = true;
        }
        if (peer->session.ready()) {
          if (++wireCounter == 0)
            ++wireCounter;
          if (!admin.begin(peer->session, wireCounter, bridgeRequest.c_str(),
                           bridgeRequest.length(), now, adminAirtime))
            finishBridge(remoteError(bridgeCorrelation, "REMOTE_UNAVAILABLE"));
        } else if (uint32_t(now - bridgeAt) > 8 * airtime + 2000) {
          finishBridge(remoteError(bridgeCorrelation, "REMOTE_UNAVAILABLE"));
          admin.clear();
        }
      } else if (admin.takeComplete())
        finishBridge(String(admin.message(), admin.messageSize()));
      else if (admin.state() == radio_admin::Transfer::State::Failed) {
        finishBridge(remoteError(bridgeCorrelation, "REMOTE_TIMEOUT"));
        peer->session.invalidate();
      }
    }
  }
  if (ota_update::enabled()) {
    admin.abort();
    return;
  }
  if (!transmitting && !deferredAck && !healthBusy()) {
    AdminPacket packet;
    size_t size;
    if (admin.poll(now, packet, size) && !sendBytes(packet, size))
      admin.abort();
  }
}
static void pollHealth() {
  uint32_t now = millis();
  unsigned count = 0;
  for (auto &peer : peers)
    if (peer.used)
      ++count;
  for (auto &peer : peers)
    if (peer.used)
      peer.health.expire(now, radio_health::freshness(count, healthAirtime),
                         healthAirtime);
  if (!tx || !radioReady || transmitting || awaiting || deferredAck ||
      remoteOutput || healthBusy() || admin.busy() || bridgeQueued ||
      ota_update::enabled() || digitalRead(21) == LOW ||
      digitalRead(14) == LOW ||
      uint32_t(now - lastProbeAt) < radio_health::spacing(healthAirtime))
    return;
  if (!direct && cuePeer && !cuePeer->session.ready() &&
      uint32_t(now - cuePeer->helloAt) < 2 * airtime + 1000)
    return;
  for (unsigned checked = 0; checked < 8; ++checked) {
    unsigned index = (nextProbe + checked) % 8;
    auto &peer = peers[index];
    if (!peer.used || !peer.session.enabled())
      continue;
    ProbePacket ping;
    lastProbeAt = now;
    nextProbe = (index + 1) % 8;
    if (peer.session.probe(ping) && sendBytes(ping, ProbeSize))
      peer.health.startProbe(now);
    break;
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);
  pinMode(21, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  Wire1.begin(48, 47);
  bool board = expander(1, 1 << 4) && expander(3, 0xc1);
  pinMode(46, OUTPUT);
  digitalWrite(46, HIGH);
  // GPS remains powered down; no GPS UART, tracker, or upload code is linked.
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  pinMode(11, OUTPUT);
  digitalWrite(11, LOW);
  delay(100);
  device_ui::beginDisplay();
  SPI.begin(16, 7, 15, 17);
  deviceId = ESP.getEfuseMac() & 0xffffffffffffULL;
  char id[13];
  snprintf(id, sizeof(id), "%012llx", (unsigned long long)deviceId);
  BLEServer *server = outputs::begin("UberClicker");
  uc::ConfigHooks hooks;
  hooks.pauseOutput = pauseOutput;
  hooks.changed = changed;
  hooks.clearBonds = outputs::clearBonds;
  hooks.statusChanged = statusChanged;
  hooks.openUpdates = []() {
    if (ota_update::enabled() || bridgeQueued || admin.busy() || transmitting ||
        awaiting || remoteOutput || deferredAck || healthBusy())
      return false;
    updateScreenRequested = true;
    return true;
  };
  if (!board ||
      !uc::configService.begin(server, id, UBERCLICKER_VERSION, hooks)) {
    ota_update::confirmBoot(false);
    Serial.println("Hardware/config initialization failed; outputs disabled");
    for (;;)
      delay(1000);
  }
  // Finish a reset before advertising or restoring any configured Wi-Fi
  // connection.
  auto resetState = reset_journal::state();
  if (resetState == reset_journal::State::Unavailable) {
    ota_update::confirmBoot(false);
    Serial.println("Reset journal unavailable; outputs locked");
    for (;;)
      delay(1000);
  }
  if (resetState == reset_journal::State::Pending) {
    bool cleared = uc::configService.factoryReset() &&
                   outputs::bondsCleared() && outputs::forgetWifi();
    if (!cleared || !reset_journal::mark(false)) {
      ota_update::confirmBoot(false);
      Serial.println("Reset incomplete; restart to retry, outputs locked");
      for (;;)
        delay(1000);
    }
  }
  outputs::securityPasskey(uc::configService.status().passkey);
  applySettings();
  configPaused = uc::configService.outputPaused();
  outputs::pause(configPaused);
  BLEDevice::startAdvertising();
  if (!uc::configService.status().claimed &&
      !uc::configService.status().recoveryRequired)
    uc::configService.enterSetup();
  ota_update::confirmBoot(radioReady || direct);
  updateSnapshot();
}
void loop() {
  ota_update::poll();
  uc::configService.setRemoteTransportAvailable(
      radioReady && !ota_update::enabled() &&
      !uc::configService.status().recoveryRequired);
  uc::configService.poll();
  if (updateScreenRequested) {
    updateScreenRequested = false;
    ota_update::open();
    uc::configService.exitSetup();
    outputs::pause(true);
  }
  if (reconfigure)
    applySettings();
  auto event =
      ui.poll(digitalRead(21) == LOW, digitalRead(14) == LOW, millis());
  switch (event) {
  case device_ui::Action::Previous:
    action(false);
    break;
  case device_ui::Action::Next:
    action(true);
    break;
  case device_ui::Action::OpenSetup:
  case device_ui::Action::OpenPairing:
    if (tx)
      remoteDisplay = device_ui::RemoteStatus{};
    uc::configService.enterSetup();
    break;
  case device_ui::Action::CloseSetup:
    uc::configService.exitSetup();
    break;
  case device_ui::Action::CycleProfile:
    profileIndex =
        (profileIndex + 1) % uc::configService.settings()["profiles"].size();
    outputs::configureEndpoint(profile()["host"] | "");
    break;
  case device_ui::Action::OpenUpdates:
    uc::configService.exitSetup();
    outputs::pause(true);
    ota_update::open();
    break;
  case device_ui::Action::CloseUpdates:
    ota_update::close();
    break;
  case device_ui::Action::CheckUpdate:
    ota_update::check();
    break;
  case device_ui::Action::InstallUpdate:
    if (snapshot.battery == device_ui::Battery::Low)
      break;
    ota_update::install();
    break;
  case device_ui::Action::FactoryReset:
    outputs::pause(true);
    if (reset_journal::mark(true))
      ESP.restart();
    else
      snapshot.outcome = device_ui::Outcome::Rejected;
    break;
  default:
    break;
  }
  bool nowPaused = paused();
  outputs::pause(nowPaused);
  if (nowPaused != lastPaused) {
    lastPaused = nowPaused;
    if (nowPaused) {
      awaiting = false;
      for (auto &peer : peers)
        if (peer.used && (admin.session() != &peer.session || !admin.busy()))
          peer.session.invalidate();
    }
  }
  outputs::poll();
  pollRadio();
  outputs::Result result;
  if (outputs::takeResult(result)) {
    snapshot.outcome = outcome(result);
    if (remoteOutput && outputPeer) {
      outputPeer->session.acknowledge(receivedCue, radioResult(result),
                                      ackLater);
      deferredAck = true;
      remoteOutput = false;
    }
  }
  uint32_t now = millis();
  if (awaiting && uint32_t(now - pendingAt) > 3 * airtime + 1000) {
    awaiting = false;
    snapshot.outcome = device_ui::Outcome::Unknown;
    if (pendingPeer) {
      pendingPeer->session.invalidate();
      pendingPeer->helloAt = now - (2 * airtime + 1000);
    }
  }
  pollAdmin();
  pollHealth();
  // Cue target and admin target are independent; no broadcast or queued cue
  // exists.
  Peer *discovery = bridgeQueued ? findPeer(bridgeReceiver)
                                 : (!nowPaused && !direct ? cuePeer : nullptr);
  if (tx && radioReady && discovery && !awaiting && !transmitting &&
      !healthBusy() && !ota_update::enabled() &&
      (!discovery->session.ready() ||
       (!admin.busy() && uint32_t(now - discovery->sessionAt) > 60000)) &&
      uint32_t(now - discovery->helloAt) > 2 * airtime + 1000) {
    Packet hello;
    discovery->session.hello(hello);
    send(hello);
    discovery->helloAt = now;
    discovery->sessionAt = now;
  }
  if (!tx && !uc::configService.status().remoteEditing && !admin.busy())
    for (auto &peer : peers)
      if (peer.used && uint32_t(now - peer.sessionAt) > 120000 &&
          uint32_t(now - peer.adminAt) > 120000) {
        peer.session.invalidate();
        peer.sessionAt = now;
      }
  static uint32_t uiAt = 0;
  if (uint32_t(now - uiAt) >= 50) {
    uiAt = now;
    updateSnapshot();
  }
  delay(1);
}
