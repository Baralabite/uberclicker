# Hardware acceptance record

Baseline: 29 October 2026. **No physical device has been flashed or validated by
this work.** Experimental build/host evidence is separate from hardware acceptance.
Stable promotion is blocked until UI review and these physical gates pass. Record
board revision/serial, firmware commit/version, app build, host/OS and test date
with every result; retain logs and photos without exposing passkeys or secrets.

## Software evidence available

| Area | Evidence | What it establishes |
| --- | --- | --- |
| UI renderer/input | `python3 scripts/build-device-preview.py`; production C++ assertions under `firmware/tests/ui/`; UndefinedBehaviorSanitizer run | Release/debounce/no-repeat, chord/RX suppression, reset gates/rollover, pause transitions and bounded bitmap rendering; mocked display-task coalescing/cadence/allocation checks. |
| Review pixels | `designs/firmware/*.pbm`, matching PNGs and `review-sheet.png` | Actual 200×200 firmware font and framebuffer content, without panel optics/timing. |
| Browser review | `device-preview.html`, three alternatives, 320/736 px review | Menu/action simulation, RX no-cue, Cancel default, early release, keyboard three-second hold, visible layout. Remote review covers separate admin/cue IDs and pending/confirmed/unknown/cached/rejected frames; new physical tests remain pending. |
| Radio/output host checks | `python3 firmware/tests/run_host.py` | Authentication/replay/session and codec assertions; actual local listener bytes for OSC, UDP and TCP. |
| Public firmware build | `docs/firmware.md`, integration build/release logs | Compile/link of public sources and hardware adapter. The integration owner records final combined build/package hashes. |
| Configuration/app | `firmware/tests/config/`, `app/tests/`, `docs/app.md` | Contract, framing, service/client state and build evidence, as recorded by their owner. Not real BLE permissions, discovery or radio coexistence. |
| Dock | `cad/exports/validation.json`, `docs/charging-dock.md` | CAD/mesh/prototype evidence. No measured fit, print or charging result. |

AddressSanitizer stalled in its host runtime initialization before test entry and
was stopped. UndefinedBehaviorSanitizer and ordinary assertions passed. No
ASan coverage is claimed. Timing/queue code uses real FreeRTOS/GxEPD2 in the device
build. The host adapter test runs that production task against mocked queue, clock
and panel calls; it verifies software policy, not panel/SPI hardware behavior.

## Pending physical tests

Every row below is **pending**. Replace with a dated result and evidence only
after actually performing it. A failed gate needs a fix and repeat evidence.

| Gate | Procedure and acceptance |
| --- | --- |
| Board/pins/power | Confirm exact M5 hardware revision, GPIO21/14 polarity, expander/radio power initialization, display wiring and battery divider. Boot on USB and battery without private config; no unintended outputs at boot. |
| Single buttons | Tap, bounce, very briefly touch, hold for >10 s, release; repeat across power-on with either/both held. Exactly one deliberate cue on release, no held repeat and no cue from startup-held input. |
| Chord safety | Press together, stagger press/release, overlap briefly and try chatter. Chord opens Menu only after valid debounce/release; no Previous/Next escapes. Non-overlapping presses remain two intentional singles. |
| RX isolation | Operate both receiver buttons in home, menus and setup while monitoring every configured output. No local receiver button generates an output cue or radio cue. Incoming authenticated cues still work when home and unpaused. |
| Reset gates | Enter reset with Cancel default; hold both without changing selection, cancel with B2, arm Erase, try individual buttons, release either at 0.1/1/2.9 s, and hold both >3 s. Only the selected, uninterrupted hold issues reset; all other paths produce no cue. Interrupt with external setup/pause as well. |
| Reset persistence | After successful reset and power cycle, old owner token, BLE bonds, Wi-Fi secret, profile and radio key must fail/be absent. Code/claim material rotates, firmware remains. Interrupt NVS removal/power where safe; failure must keep output paused and avoid a false success screen. Check OS-side stale bonds/re-pairing. |
| Full refresh | Measure real full-update duration, readability of small text, clipping, orientation, black/white polarity and ghosting. Confirm no start interval <2.5 s, no identical-frame flashes and no animated countdown. Review TX, RX, all menu selections, setup/edit and reset screens on the actual panel. |
| Responsiveness under refresh | Log raw input, cue submission and radio timestamps while full refresh is busy. Burst clicks, incoming radio cues and setup requests must remain responsive; only latest queued screen survives. No stale command is replayed when the panel catches up. Test queue/task allocation failure handling. |
| Battery | Compare ADC/divider voltage with a meter at known discharge levels, USB inserted/removed and power transitions. Calibrate coarse bands or keep unknown. No percentage/charging inference; avoid claiming remaining runtime from a bar. |
| RF pair/session | Pair two real units, verify both roles/IDs/keys and incomplete-pair behavior. Check authenticated receipt/result semantics, duplicates, wrong key/peer, forged ACK, reboot of either end, captured stale traffic and pause/session renewal. Never observe a duplicate output or automatic cue retry. |
| Multiple RX target isolation | Physically pair one Tx with at least two Rx using distinct keys/IDs. Configure Rx B while cue destination remains Rx A. Only B's settings/revision may change; A receives no configuration or unintended cue. Explicit Use for cues changes destination separately; no implicit fanout or Apply-all. Repeat with wrong/removed controller and forged/mismatched target/session. |
| Remote secrets and uncertainty | Capture actual management radio traffic: Wi-Fi secrets must be protected by authenticated encryption. Lose fragments/replies, especially Apply response; UI must retain target identity and show pending/unknown/rejected/cached truthfully. Reread actual Rx revision after loss; duplicate management Apply must not commit twice. Never reuse a cached success for another Rx. |
| Remote lease/recovery | Exercise long-range transfers, 120 s without fresh authenticated progress, one-hour absolute transfer ceiling, app disconnect, Tx setup exit, lost Cancel, local-vs-remote edit conflict and reboot. New valid progress may renew idle recovery; duplicate/replayed traffic may not. Managed RX must recover from abandonment without an hour-long pause or output replay, while its guarded physical reset remains available. |
| Durable Apply receipts | Cut power after a settings commit but before its reply. After reboot, query the same controller/pair/transaction ID: only a stored matching receipt may report applied/revision. Unknown/evicted receipts stay unknown; no automatic reapply. Repeat the same transaction/content without another commit; conflicting reuse must fail. Check last-eight history bounds, wrong receiver/pair/controller and torn receipt+settings persistence. |
| Migration and unsupported settings | Migrate a real v1 pair: cues retain their key, but admin authority defaults false on both sides. Verify one-sided grants cannot administer an RX; explicit physical approval on both enables it. Boot unsupported/corrupt settings: claim/auth/config and cues must remain blocked, Recovery menu remains operable, and only guarded physical reset clears it. |
| Minimal TX health UI | Verify huge TX and only battery/AVG/fresh-connected-over-paired on home. With one paired RX missing and others strong, retain the strong average AND a separate missing warning. Never count unpaired, stale, duplicate or invalid signal samples. Initial/unsupported metrics stay unknown; a known empty roster is 0/0. Receiver status pages show every paired ID with fresh RSSI or explicit Offline/Unknown. Test authenticated probe replay/forgery and freshness through a full eight-peer round at both presets; compare actual readings with radio logs. |
| RF timing/range | Measure 80-byte airtime, handshake/ACK latency and rejection windows under SF7/SF12, weak signal and packet/ACK loss. Check 24-bit clock estimate tolerance and sustained scheduling load. Unknown means unknown; no live-link indicator. Use appropriate regional hardware/settings. |
| Keyboard release | Pair with real target OS using displayed MITM passkey. Subscribe, exit setup and verify exact mapped keys/modifiers. Disconnect/power-cycle during press; reconnect must send release before accepting another cue, with no replay. Test actual slideshow focus and application effect separately. |
| BLE MIDI release | Connect using each intended OS MIDI tool. Check note/channel/value, Note Off, CC/program behavior, timestamps, all-notes-off on pause/reconnect and no stuck/replayed notes under disconnect. Monitor real MIDI routing and notification failures. |
| Shared BLE lifecycle | Test config+HID+MIDI services on one server, permitted reconnection, one-client limit, subscriptions and advertisement/service discovery. Verify keyboard Just Works bonds do not grant configuration; only actual encrypted+authenticated links pass. |
| Mobile permissions | Android/iOS permission denied/granted/revoked, native reconnect, app background/foreground, secure token storage and disconnect mid-fragment/edit/Apply. Simulator build is not an iPhone BLE test. |
| Optional browser configuration | If retaining the development browser client, verify its supported Web Bluetooth flow, unsupported-browser notice and memory-only token handling. This is optional; native BLE is the configuration acceptance path. No LAN/backend/hosted configuration service is required. Browser USB flashing remains a required, separate gate below. |
| Edit/pause | While local/direct and radio output are active, open setup/edit; every route pauses and releases active keys/notes. Apply/Cancel/expiry/disconnect must not replay a held input or queued cue. Test stale revisions and interruption after commit before response. |
| Wi-Fi failure | Wrong password, absent SSID, DHCP/DNS failure, changed network and power loss during Apply. BLE setup must remain recoverable. Applied settings do not claim a successful join; secret reads remain redacted. |
| Network output | Use real destination listeners from `docs/user-guide.md`. Verify OSC bytes/types, one UDP datagram and LF-framed TCP across partial reads. Disconnect/reset destination mid-write and load DNS/connect. Never resend possibly delivered output; input/radio continue while network worker blocks. |
| USB install/recovery | On an explicitly authorized test device, verify board/flash detection, correct manifest offsets/hashes, settings-preserving update, cancelled/disconnected install, bad image rejection and bootloader recovery over CH340. Record actual readback and boot evidence. |
| Native USB capability | Trace connector/schematic and GPIO19/20 on actual board before any USB HID/MIDI claim. CH340 serial programming does not satisfy this gate. Current capability remains unavailable. |
| Dock/charging | Measure body, socket/plug geometry and insertion direction; print coupon before cradle. Check connector float, cable bend/strain relief, device weight on floor, retention/removal and charging with the selected supply/cable. Current exports are fit prototypes. |

## Record template

Copy one record for each run:

```text
Date / operator:
Board revision / device IDs:
Firmware commit / version / manifest hash:
App artifact / OS / target application:
Gate and exact procedure:
Expected:
Observed (including timestamps where relevant):
Pass / fail / pending:
Evidence path (redacted log, photo or capture):
Follow-up and repeat-test result:
```

Do not mark a feature hardware-validated from one happy-path click. Keep failed,
unsupported and unavailable capabilities visible in the release notes. The
[UI](device-interface.md), [user guide](user-guide.md), [firmware](firmware.md),
[outputs](outputs.md), [installation](flashing.md) and
[dock guide](charging-dock.md) describe the current narrower behavior.

## Opt-in OTA acceptance

- Install the two-slot layout once over USB with explicit erase consent; reclaim,
  configure Wi-Fi and pair again. Confirm app/config status reports the release version.
- Open physical setup and authorize the local BLE app. Confirm **Open update screen**
  opens the screen without a network request or flash write; remote Rx administration,
  unauthenticated requests and active edits cannot open it. Verify physical B1 Check
  and B1 Install are separately required.
- Outside an explicitly active update session, verify no feed/image requests occur. B2 exits
  before installation; checking alone must not write an app or select a boot slot.
- With verified HTTPS and NTP reachable, check the mirrored experimental version,
  confirm B1 Install, watch progress and restart, and verify settings/bonds survive.
- Try no Wi-Fi, blocked NTP, invalid TLS, redirect, incompatible layout/schema,
  truncated image, incorrect SHA-256 and low battery. Confirm useful error/exit
  behavior, no cue output and no switch away from the running image.
- Interrupt power during download/write and during OTA-data switching; confirm
  recovery to a valid image and successful retry. Test both app0 → app1 and app1 → app0.
- Test a pending image whose hardware/config/radio initialization fails. Confirm
  bootloader rollback to the previous image; measure free heap with BLE, display
  and TLS active. Host mocks cannot establish these physical guarantees.

The minimal TX menu remains Receiver status, Pair new RX and Factory reset.
OTA uses the authorized local BLE app to request its physical confirmation screen.
Browser USB flashing/recovery remains required.
