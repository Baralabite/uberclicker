#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEServer.h>

// Core owns Arduino BLE (NimBLE-backed in Arduino 3.3.11) lifecycle/security.
// Never create another server or call BLEDevice::init from this service.
namespace uc {
constexpr uint8_t kConfigSchemaVersion = 2, kConfigProtocolVersion = 2;
constexpr size_t kConfigMaxMessage = 8192;
constexpr const char *kConfigServiceUuid =
    "8f4e1000-6b8d-4b37-9f4a-75b61e350001";
constexpr const char *kConfigWriteUuid = "8f4e1001-6b8d-4b37-9f4a-75b61e350001";
constexpr const char *kConfigNotifyUuid =
    "8f4e1002-6b8d-4b37-9f4a-75b61e350001";

struct ConfigStatus {
  bool setup = false, editing = false, claimed = false;
  bool remoteEditing = false, remotePending = false, recoveryRequired = false;
  String remoteDeviceId;
  uint32_t revision = 0;
  // Six decimal digits: core displays this and uses it as BLE MITM passkey.
  uint32_t passkey = 0;
  String deviceId;
};
struct ConfigHooks {
  // Invoked from poll(), never from BLE callback. Release HID/MIDI on pause.
  void (*pauseOutput)(bool) = nullptr;
  void (*changed)() = nullptr;
  // Core clears ALL BLE bonds and restarts security after reset/revocation.
  void (*clearBonds)() = nullptr;
  void (*statusChanged)(const ConfigStatus &) = nullptr;
  // Requests a local update screen only; download/install still need device buttons.
  bool (*openUpdates)() = nullptr;
};
class ConfigService {
public:
  bool begin(BLEServer *existingServer, const String &deviceId,
             const String &firmwareVersion, ConfigHooks hooks = {});
  void poll();
  void enterSetup(uint32_t durationMs = 120000);
  void exitSetup();
  // Core forwards authenticated security state; encrypted Just Works is false.
  // One config connection at a time. Forward disconnect before advertising
  // again.
  void onSecurity(uint16_t connection, bool encrypted, bool authenticated);
  void onDisconnect(uint16_t connection);
  const JsonDocument &settings() const;
  ConfigStatus status() const;
  bool outputPaused() const;
  // Secrets available to core only; never serialize into readable settings.
  String wifiPassword() const;
  String pairKeyHex(const String &deviceId = "") const;
  // Core radio bridge: queued only by an authorized Tx BLE client.
  // correlation is an opaque monotonic bridge ID, independent of BLE request
  // ID.
  bool takeRemoteRequest(String &receiverId, String &request,
                         uint32_t &correlation);
  void completeRemoteRequest(uint32_t correlation, const String &response);
  bool remoteRequestActive(uint32_t correlation = 0) const;
  void setRemoteTransportAvailable(bool available);
  // Core calls ONLY for new authenticated accepted progress, never duplicates.
  void noteRemoteProgress(const String &peerId);
  bool takeRemoteCancellation(String &receiverId, String &session);
  // Call only after authenticated encryption/replay/target checks by radio
  // core. Receiver permits its paired controller to edit output/profiles/Wi-Fi
  // remotely.
  String processRemoteRequest(const String &controllerId,
                              const String &request);
  bool factoryReset();

private:
  struct Impl;
  Impl *impl_ = nullptr;
};
extern ConfigService configService;
} // namespace uc
