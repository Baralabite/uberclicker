#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
// Configuration is available only over trusted USB serial.
namespace uc {
constexpr uint8_t kConfigSchemaVersion = 4, kConfigProtocolVersion = 2;
constexpr size_t kConfigMaxMessage = 8192;

struct ConfigStatus {
  bool setup = false, editing = false, claimed = false;
  bool remoteEditing = false, remotePending = false, recoveryRequired = false;
  String remoteDeviceId;
  uint32_t revision = 0;
  // Six decimal digits used only for BLE keyboard/MIDI host pairing.
  uint32_t passkey = 0;
  String deviceId;
};
struct ConfigHooks {
  // Release HID/MIDI on configuration pause.
  void (*pauseOutput)(bool) = nullptr;
  void (*changed)() = nullptr;
  // Core clears ALL BLE bonds and restarts security after reset/revocation.
  void (*clearBonds)() = nullptr;
  void (*statusChanged)(const ConfigStatus &) = nullptr;
  // Requests a local update screen only; download/install still need device buttons.
  bool (*openUpdates)() = nullptr;
  bool (*clearCloudAccount)() = nullptr;
};
class ConfigService {
public:
  bool begin(const String &deviceId, const String &firmwareVersion,
             ConfigHooks hooks = {}, const String &defaultDeviceName = "");
  void poll();
  void enterSetup(uint32_t durationMs = 120000);
  void exitSetup();
  const JsonDocument &settings() const;
  ConfigStatus status() const;
  bool outputPaused() const;
  // Secrets available to core only; never serialize into readable settings.
  String wifiPassword(const String &ssid = "") const;
  String cloudRoom() const;
  String cloudToken() const;
  String pairKeyHex(const String &deviceId = "") const;
  // Legacy radio bridge API; remote configuration is unsupported.
  // correlation is an opaque monotonic bridge ID.
  bool takeRemoteRequest(String &receiverId, String &request,
                         uint32_t &correlation);
  void completeRemoteRequest(uint32_t correlation, const String &response);
  bool remoteRequestActive(uint32_t correlation = 0) const;
  void setRemoteTransportAvailable(bool available);
  // Core calls ONLY for new authenticated accepted progress, never duplicates.
  void noteRemoteProgress(const String &peerId);
  bool takeRemoteCancellation(String &receiverId, String &session);
  // Retained for legacy callers; always rejects remote configuration.
  String processRemoteRequest(const String &controllerId,
                              const String &request);
  String processSerialRequest(const String &request);
  bool factoryReset();

private:
  struct Impl;
  Impl *impl_ = nullptr;
};
extern ConfigService configService;
} // namespace uc
