# Implementation and delegation plan

Historical planning baseline: 29 October 2026. Later user changes supersede the
single-pair and LAN/hotspot targets below: BLE-only configuration is sufficient,
Tx manages up to eight independently keyed Rx over encrypted LoRa after initial
physical pairing, and the device UI uses the large TX home and three-item menu.
The rounded dock has a 25% wider base and opposing cable tabs. Current behavior
and evidence live in [README](../README.md), [protocol](../contracts/config-protocol.md)
and [integration checks](integration-checks.md).

This document authorizes no hardware flashing
or stable-release claim. The website design is approved; the device UI remains
reviewable. Existing product, configuration and interface documents describe the
intended behavior. Implementation owners must reconcile their documentation with
what they actually build and verify.

## Execution rules

Use five new GPT-6.1 Sol chats. The parent chat orchestrates; implementation belongs
to the workers. They share this repository's working tree, so ownership below
is exclusive. No worker independently commits, pushes, tags, publishes or changes
branches. The release/integration owner performs authorized Git/publication actions
serially after integration. Coordinate before editing another owner's files.

The separate sibling `../gathermesh-lora` repository is a read-only hardware/software reference.
Never mutate it or copy private `config.local.h`, credentials, backups or binaries.
Neither repository had `.codegraph/` when inspected; do not create an index.
Public firmware must build without a private configuration file or embedded keys.

Each worker reports changed paths, exact successful checks, failures, artifacts,
and remaining physical validation. Mocks, simulations and builds are not hardware
verification. Preserve the approved website design and coordinate with the existing
deployment chat before editing its files. Account inventory and remote relay are
an optional later phase, not a dependency of local configuration.

## Shared configuration and transport contract

The configuration/app owner publishes `contracts/config.schema.json`,
`contracts/config-protocol.md`, representative fixtures and integration headers
before firmware/client implementations diverge. This is the first milestone,
not a separate planning project. Freeze exact UUIDs, operation names and framing
there; all clients and firmware use those values. Unresolved library details are
that owner's implementation responsibility.

Required settings and metadata:

- Separate configuration schema version, configuration revision, firmware version,
  board ID, device ID and capability flags.
- Role `tx` or `rx`; transmitter route `lora` or `direct`.
- Explicit outputs: `ble_keyboard`, `ble_midi`, `osc_udp`, `udp`, `tcp`.
  Native USB HID/MIDI are unavailable capabilities until board wiring is verified.
- Bounded named profiles, active profile, Previous/Next mappings, output endpoints
  and payloads, Wi-Fi configuration, radio preset and paired-peer metadata.
- Separate write-only secrets from readable configuration. Explicit keep, replace
  and clear operations; empty fields must not ambiguously preserve or erase secrets.

Required operations: capabilities/status, read configuration, begin edit, validate,
apply with expected revision, cancel, and physically authorized pairing. Responses
carry request correlation and bounded errors. Define maximum complete-message
size, GATT fragmentation/reassembly, ordering, timeout and disconnect behavior.
Use one logical request/response model across BLE and LAN, with transport-specific
framing only where necessary. Include cross-language conformance fixtures.

One edit session pauses cue output and visibly indicates this. Validate the entire
staged configuration before atomic commit. Reject stale revisions. Cancel, expiry
and disconnect discard staged edits and resume operation according to a documented
policy. Failed Wi-Fi changes retain a local recovery path. Persistence, reset and
power-loss behavior must be explicit.

BLE access requires a physical setup window and authenticated encryption. The
reference HID's Just Works bond alone must not authorize configuration. Define
fresh claim, subsequent authorization, bond handling and revocation. The firmware
must have one BLE stack/server lifecycle shared by HID, MIDI and configuration;
the app owner and firmware owner agree that boundary before implementation.

LAN requires an explicit authentication and transport-protection design before
clients implement it. Do not silently transmit persistent credentials over
unprotected HTTP. Prefer initial authorization over BLE and native LAN reuse of
that authorized relationship. If protected LAN provisioning is not completed,
keep working BLE provisioning and a physically enabled WPA2 setup-hotspot fallback,
report the exact narrower LAN capability, and do not mark unrestricted LAN setup
complete. A hosted HTTPS page cannot be assumed to access local HTTP devices.

The firmware configuration header must expose initialization/polling, setup
entry/exit, immutable committed settings, configuration-change notification and
reset integration. Agree a small UI/status/event interface at the same milestone.
Do not build independently invented JSON models in each component.

## 1. Configuration service and native/browser app

Ownership: `contracts/config*`, configuration fixtures, `app/`,
`firmware/include/config_service.h`, `firmware/src/config_service.cpp`,
configuration-specific tests, `docs/configuration-protocol.md`, `docs/app.md`.

Implement the shared contract and matching firmware service. Build iOS/Android
apps with Capacitor, plain TypeScript, a small bundler and
`@capacitor-community/bluetooth-le`; reuse UI and protocol code for a supported
browser build. No account backend, Flutter installation, React stack or general
state-management framework is needed. Pin mutually compatible dependencies.

Implement discovery, connection, physical claim, read/edit/Apply/Cancel, profiles,
output mappings, Wi-Fi provisioning, device/version display, disconnect recovery
and pair-two-devices setup. Provide native LAN access with manual IP and mDNS where
feasible. Capability-detect web transports; stock iOS browsers receive a truthful
native-app or local-Wi-Fi fallback. Secure reusable credentials in platform secure
storage rather than ordinary preferences. No fake successful device interactions.

Pairing may provision the same generated random pair key sequentially into two
physically authorized devices. Keep incomplete pairing visible and recoverable;
discard temporary app-held pair material after completion. Do not expose stored
radio keys through ordinary settings reads.

Build an Android debug APK and unsigned iOS simulator app. Check protocol fixtures,
fragmentation, malformed/oversized messages, unauthorized access, conflicting edits,
secret redaction, expiry/disconnect, interrupted persistence and provisioning
recovery. Real BLE permissions/pairing, LAN discovery and HID/config coexistence
remain hardware acceptance tests. Produce real transport implementations as well
as explicitly labelled simulator fixtures.

## 2. Clean firmware, radio and standalone outputs

Ownership: `firmware/platformio.ini`, board/build configuration, core/radio/output
sources and headers, related tests, `docs/firmware.md`, `docs/outputs.md`.
Exclude configuration-service and UI files owned by other workers.

Create a clean PlatformIO project under `firmware/`. Start from the reference's
pinned board/toolchain and verified initialization, not a copy of its whole
application. Remove GPS, tracker uploads and private compile-time key requirements.
Preserve documented hardware pins, 4 MB flash configuration, CH340 programming and
conservative 115200 upload speed until evidence warrants a change.

Implement Tx/Rx, debounced release-based buttons, chord suppression and no held
repeat. No unexpected queued cues. Integrate the agreed configuration/UI interfaces.
Keep display refresh and potentially blocking network operations out of the
input/radio handling path. Reuse BLE keyboard report/release behavior carefully.

Standalone Tx supports BLE keyboard, BLE MIDI, OSC over UDP, raw UDP and raw TCP.
BLE MIDI is the chosen MIDI transport; USB MIDI and RTP-MIDI are outside this
release. Support bounded channel/note/CC/program mappings with correct release
behavior. Specify OSC argument types and raw TCP framing. Never replay a possibly
delivered cue automatically after reconnect. Validate endpoints/payload lengths.

Replace the unauthenticated one-byte radio format with a versioned authenticated
single-pair protocol: per-pair random keys, device identities, authenticated session
establishment, fresh receiver session challenges, sequence validation and duplicate
suppression. Use established mbedTLS primitives, no public default key and no silent
legacy fallback. Specify exact packet bytes and bounds; test both directions.
Ensure sender/receiver restart cannot turn recorded old commands into new cues.

No automatic cue retry. Separate receipt, local output submission, rejection and
unknown outcome. Recalculate deadlines from actual packet airtime: the reference's
one-byte SF12 latency figures no longer apply. A radio ACK never proves application
effect; UDP completion means sent, not delivered.

Reset clears settings, Wi-Fi credentials, profiles, local authorization, radio keys
and BLE bonds, rotates claim material, preserves firmware and returns to unclaimed
setup. It cannot promise removal of an offline cloud inventory record.

Checks: clean public PlatformIO build; button/chord/clock-rollover tests; forged or
wrong-peer messages and ACKs; duplicate/stale-session/reboot cases; no output while
configuring; key/note release recovery; real local UDP/TCP receivers checking bytes
and OSC encoding; bounded failure/reconnect behavior. Do not flash attached devices.

## 3. Releases, browser flasher and final integration

Ownership: firmware CI/release workflows and packaging scripts,
`contracts/release.schema.json`, `website/dist/flash/`, published release asset
paths, `docs/flashing.md`; final README and status reconciliation. This owner alone
edits existing website integration points and `.github/workflows/pages.yml`, after
the deployment chat finishes. Coordinate app artifact copying with the app owner.

Build/test firmware on pull requests and main; package versioned releases from
explicit tags. Pin dependencies/tools, record source commit/version and reject
invalid release inputs. Produce real bootloader, partition and application images
and a manifest identifying board/chip, flash size, firmware/settings/protocol
versions, source commit, exact offsets, filenames, sizes and SHA-256 hashes. Derive
offsets from the actual build. Never copy generic ESP32 offsets. Include factory
images only with explicit erase consequences; builds contain no credentials.

Use pinned `esptool-js` for the browser flasher. Add a small navigation/section link
without redesigning the approved site. Require explicit M5 board selection; check
detectable ESP32-S3/flash compatibility, manifest bounds/overlaps and downloaded
hashes before writing. Expose progress, failures and recovery. Preserve settings
by default when the release layout permits; never silently fall back to full erase.
Chip detection alone does not verify the physical board model.

Use a tested browser-fetchable asset source. Prefer exact release bytes mirrored
under versioned same-origin Pages paths with a release index instead of assuming
GitHub release downloads provide suitable CORS. Preserve previous versions and
verify site/release hash equality. Do not fabricate a working release when no
published assets exist. Keep prerelease and stable selection explicit.

Run packaging/CI dry runs, manifest corruption and wrong-chip tests, unsupported
browser/cancel/disconnect tests, deployed-origin download checks and existing site
checks. Serial mocks do not establish a successful physical flash.

After all workers finish, own final integration: reconcile interfaces through their
owners, run combined builds/tests, update documentation and feature status, then
perform authorized commit/push/publication serially. No stable hardware release
before UI review and physical acceptance. Prerelease packaging is separate from
stable promotion; never infer permission to flash hardware from website approval.

## 4. Parametric USB charging dock

Ownership: `cad/`, `docs/charging-dock.md` (execution override agreed after planning).

Create an editable OpenSCAD upright walkie-talkie-style cradle with USB power
entering through the base and a retained cable/connector reaching the actual M5
USB-C socket. Inspect manufacturer references for socket position and insertion
direction first. Do not assume it faces downward. A side-facing socket can require
a right-angle connector or a different insertion mechanism.

Unmeasured dimensions must be named parameters and visibly labelled. Parameterize
body envelope/taper, socket position/orientation, pocket clearance, lead-in,
insertion stop, connector float, cable bend clearance, strain relief, fasteners
and removable bottom cover. Support device weight through the cradle rather than
the USB connector. Use an existing cable/connector assembly; no new charging circuit.

Provide a small fit/connector coupon, SCAD, STL and rendered previews, parameter
guide, assembly instructions and a measurement worksheet. Required measurements:
body envelope, socket center/orientation/recess, selected plug envelope, insertion
travel and cable exit/bend clearance. All unmeasured exports are fit prototypes,
not print-ready or manufacturing-validated parts.

Checks: successful OpenSCAD renders/exports, nonempty valid meshes, parameter sanity
and assembly/cable-path visual inspection. Physical measurement, test print,
insertion/retention and charging remain acceptance tests.

## 5. E-paper UI and usage documentation

Ownership: `firmware/include/device_ui.h`, `firmware/src/device_ui.cpp`, UI tests
and assets, `designs/`, `device-preview.html`, preview build script,
`docs/device-interface.md`, `docs/user-guide.md`, `docs/hardware-validation.md`.

Translate quiet action-first into a real 200x200 monochrome renderer and menu state
machine using the agreed core/config interfaces. Preserve other design alternatives
for review. Implement Tx/Rx home, profile/action labels, truthful receipt/errors,
coarse battery, setup/paused screens and navigation. Receiver buttons emit no cue.

Reset starts on Cancel, requires explicit selection and then a continuous
three-second two-button hold. Early release cancels without leaking an ordinary
click. Use latest-frame queue/coalescing and bounded refresh frequency; repeated
identical actions need no refresh. Preserve input/radio responsiveness during the
slow display update. No inferred charging state, calibrated percentage or
application-success claim.

Produce review images using actual firmware font metrics/rendering where practical,
not only the HTML approximation. Keep design source and generated preview in sync.
Check screen/text bounds, navigation, reset protections, output suppression and
representative rendered images. Actual panel readability remains physical testing.

Document implemented end-to-end installation/recovery, claim/pairing, Tx/Rx, each
standalone output with a working example, native/browser/BLE/LAN limits,
Apply/Cancel, reset, troubleshooting and dock links. Distinguish implemented,
bench-tested and hardware-validated features. Hardware checklist includes gestures,
display refresh under clicks, RF, BLE reconnection, MIDI release, failed
provisioning/reset and USB recovery.

## Order, available tools and acceptance gates

1. Start configuration/contract, dock, and UI/docs work. UI can begin from the
   existing proposal while shared headers settle.
2. Start core firmware once contract and interfaces are concrete. The app continues
   in parallel. Release scaffolding can start against agreed build/manifest paths.
3. Postpone existing deployment-file changes until the deployment chat finishes.
   Each worker fixes only its own files during integration.
4. The release owner runs one combined integration pass and serial Git actions.
5. Present device UI, dock prototype, builds and evidence for review. Hardware
   testing and stable promotion follow the appropriate approval/acceptance gate.

Observed tools: Node 22, PlatformIO 6.1.19 with ESP32 packages, Xcode 26.3,
Android Studio and SDK platforms 35/36, and OpenSCAD. These support real firmware,
Android debug, unsigned simulator and CAD builds. Signed iOS distribution and app
store delivery still require the user's signing/account configuration.

Outstanding physical evidence: M5 native USB wiring, exact dock geometry, input and
e-paper timing, RF performance with the new packet size, BLE HID/MIDI/config
coexistence, mobile permissions/reconnects and actual flash/recovery. Source evidence
supports CH340 serial programming, not native USB HID. Local tools do not resolve
these hardware questions.

Primary implementation references:

- [Capacitor environment requirements](https://capacitorjs.com/docs/getting-started/environment-setup)
- [Capacitor Bluetooth LE plugin](https://github.com/capacitor-community/bluetooth-le)
- [Espressif esptool-js](https://github.com/espressif/esptool-js)

This plan was produced through read-only inspection; only this planning document
was subsequently added. No implementation or hardware changes were made.
