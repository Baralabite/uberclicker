#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ota_update.h"

// UI is a presentation boundary, not a second configuration model. Core copies
// committed config/status into Snapshot; UI never owns BLE/radio/output lifecycles.
namespace device_ui {
enum class Role : uint8_t { Tx, Rx };
enum class Route : uint8_t { LoRa, Direct };
enum class Battery : uint8_t { Unknown, Low, Medium, High };
enum class Outcome : uint8_t { None, Pending, Received, Submitted, Sent, Rejected, Unknown };
enum class Setup : uint8_t { None, App, Claim, Pairing, Editing, RemoteEditing };
// Presentation status for the contract-v2 receiver operation; not persisted
// settings. Confirmed requires a correlated authenticated ok:true response; rejection
// and cached data must retain their own states.
enum class RemoteState : uint8_t { None, Pending, Confirmed, Unreachable, Unknown, Cached, Rejected };
enum class RemoteOperation : uint8_t { Status, Read, Begin, Validate, Apply, Cancel, Result };
struct RemoteStatus {
  RemoteState state = RemoteState::None;
  RemoteOperation operation = RemoteOperation::Status;
  bool editing = false; // actual receiver edit lease, never inferred from a send
  bool resultApplied = false; // Result reply state == applied, not merely ok:true
  char deviceId[33] = {}; // Tx: selected admin Rx; Rx: authenticated controller
  char name[33] = {};
};
// Core supplies configured membership and authenticated probe results. UI never
// infers a link from a cue send, cached ACK, RSSI alone, or an unpaired packet.
enum class ReceiverState : uint8_t { Unknown, Connected, Offline };
struct ReceiverLink {
  bool paired = false, fresh = false, signalValid = false;
  ReceiverState state = ReceiverState::Unknown;
  int16_t rssiDbm = 0;
  char deviceId[33] = {}, name[33] = {};
};
struct ReceiverSummary {
  bool rosterKnown = false, monitoring = false, hasAverage = false;
  uint8_t paired = 0, connected = 0, offline = 0, unknown = 0, samples = 0;
  int16_t averageRssiDbm = 0;
  uint8_t averageBars = 0;
};
enum class Action : uint8_t {
  None, Previous, Next, CycleProfile, OpenSetup, CloseSetup, OpenPairing, FactoryReset, OpenUpdates, CloseUpdates, CheckUpdate, InstallUpdate
};
enum class View : uint8_t { Home, Menu, Device, Setup, ResetChoice, ResetArmed, Updates, Receivers };
struct Snapshot {
  Role role = Role::Tx;
  Route route = Route::LoRa;
  Battery battery = Battery::Unknown;
  Outcome outcome = Outcome::None;
  Setup setup = Setup::None;
  ota_update::Status update;
  RemoteStatus remote;
  bool receiverRosterKnown = false, linkMonitoringAvailable = false;
  ReceiverLink receivers[8];
  // Copied from schema-v2 peers/activePeer. Admin selection NEVER changes this.
  char cueReceiverId[33] = {};
  bool paused = false;
  bool recoveryRequired = false; // ConfigStatus: no claim/config until physical reset
  bool paired = false;
  char profile[33] = "Presenter";
  char previous[33] = "Previous";
  char next[33] = "Next";
  char output[33] = "BLE keyboard";
  char lastAction[33] = "";
  char deviceName[33] = "UberClicker";
  // Core supplies real claim/setup text; never put invented credentials here.
  char setupLine1[33] = "Open UberClicker app";
  char setupLine2[33] = "";
};
ReceiverSummary summarizeReceivers(const Snapshot &snapshot);
struct Frame {
  static constexpr unsigned Width = 200, Height = 200, Bytes = 5000;
  uint8_t pixels[Bytes] = {}; // row-major, MSB left; 1 = black
};
class Controller {
 public:
  // Call setSnapshot and poll on the input/core task only. Values are copied.
  void setSnapshot(const Snapshot &snapshot);
  // Raw pressed levels, sampled frequently. Includes debounce, release-only
  // actions, startup-held inhibition, chord suppression and reset hold timing.
  Action poll(bool b1, bool b2, uint32_t nowMs);
  bool cuesPaused() const;
  View view() const { return view_; }
  unsigned selection() const { return selection_; }
  void render(Frame &frame) const;
 private:
  Action gesture(unsigned button);
  Snapshot snapshot_;
  View view_ = View::Home;
  unsigned selection_ = 0;
  bool ready_ = false, raw_[2] = {}, stable_[2] = {}, seen_[2] = {};
  uint32_t changed_[2] = {};
  bool chord_ = false, chordReady_ = false, hold_ = false, draining_ = false;
  uint32_t holdStart_ = 0;
};
// Hardware adapter owns one FreeRTOS display task and a length-one overwrite
// queue. submit never waits for the ~2 s panel refresh; identical frames skipped.
// beginDisplay returns false on allocation failure; core must remain usable.
bool beginDisplay();
void submitDisplay(const Frame &frame);
uint32_t displayRefreshCount();
} // namespace device_ui
