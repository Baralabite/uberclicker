# Integration evidence — 29 October 2026

The v2 software checks below pass in the completed development tree, including
the compact-storage fix and local OTA-screen hook. No serial port was opened and
no physical device was flashed. This does not complete device UI or hardware
acceptance. The focused independent recovery/security review passed before the
final storage/OTA integration changes; those changes have dedicated host checks
and the combined firmware build below.

- Production radio/output host checks: eight independently keyed peers, authenticated
  encryption, forged/replayed packets, reboot/session freshness, 8 KB management
  transfers, lost-ACK deduplication and bounded failures. Actual local OSC/UDP/TCP
  listeners verify the output bytes; cancelled/refused TCP sends do not retry.
- Production configuration-service host checks: Tx/two-Rx authority and target
  isolation, migrated grants, 120-second fresh-progress leases, lost Begin/Cancel,
  revisions, durable Apply receipts/result lookup after reboot, conflicting IDs,
  stale cross-target callbacks, torn writes and locked unknown-schema recovery.
- Storage capacity: compact NVS blobs retain legacy string reads and reclaim only
  the inactive slot. Four profiles/eight peers with max-length quote-heavy fields survive
  repeated saves, reboot and torn replacement. The largest exercised blob is 6,446
  bytes; peak modeled usage is 15,232 bytes including a 2 KB driver reserve, below
  the five-page 16,128-byte usable-entry budget. A failed readback locks outputs;
  reboot resolves the durable receipt without replay. Physical garbage collection
  and power-cut behavior remain unverified. The previous double-escaped string
  exceeded [ESP-IDF's NVS string limit](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html).
- Fresh radio-health probes: authenticated nonce matching, replay/forgery rejection,
  no session-counter reset, expiry/rollover and unknown-signal handling. Pairing
  alone never marks a receiver online. SF12/eight-peer freshness spans minutes.
- UI/display: all 69 firmware-derived frames match the preview. Minimal TX/menu,
  paired-only RSSI/counts, paging, buttons/reset, receipts and recovery assertions
  pass. Display checks cover allocation failure, nonblocking submission, latest-frame
  coalescing and refresh cadence. UI owner also passed UBSan; ASan is not claimed.
- App: all 19 tests pass, including schema/framing, credential storage policy,
  multi-receiver separation, durable unknown-Apply recovery and the local-only OTA
  screen request. Production browser build succeeds.
- Release tools: 12 packaging tests plus 12 installer/mirror tests pass. They cover
  embedded version/project, separate schema/protocol/framing/radio metadata,
  corruption/chip/layout rejection, explicit preserve/erase consent, readback,
  cancellation, unsupported browsers and exact historical-release mirroring.
- OTA runtime host checks pass: opt-in session, compatible feed, TLS policy, Wi-Fi
  failure, inactive slot, truncation/hash/descriptor validation, restart and boot
  confirmation. These are mocks, not bootloader or power-loss evidence.
- Website build/check passes with the real app and current device preview copied
  into the generated distribution. Existing homepage simulation stays labelled.

Reproduce with `python3 firmware/tests/run_host.py`,
`firmware/tests/config/run.sh`, `python3 scripts/build-device-preview.py --check`,
`npm test --prefix app`, `python3 scripts/tests/test-package-release.py`,
`python3 firmware/tests/ota/run.py`,
`node --test website/flash.test.mjs scripts/tests/mirror-releases.test.mjs`,
`node scripts/build-website.mjs` and `node website/check.mjs`.

## Combined firmware package

The pinned PlatformIO build passes with explicit local version
`0.2.0-integration.1`. Application size: 1574144 bytes, within each
0x1f0000-byte OTA slot. The four actual uploader images are bootloader at 0,
partitions at 0x8000, boot-app metadata at 0xe000 and firmware at 0x10000.
Schema 2, logical configuration protocol 2, BLE framing 1, cue radio 1 and remote
management radio 2 are recorded independently. All image hashes and the source
archive CRC pass; the archive includes the final source, libraries/core and licenses
without private configs or the reference project.

This local package correctly carries `sourceDirty:true`; the browser/mirror reject
it for publication. [Firmware CI](https://github.com/Baralabite/uberclicker/actions/workflows/firmware.yml)
rebuilds publishable assets from a clean commit. Never relabel local dirty output
as a clean release.

## Native development artifacts

Android rebuilt successfully with JDK 21/SDK 36; the unsigned iOS simulator app
rebuilt with Xcode 26.3. Every final browser asset, LICENSE and notice was compared
byte-for-byte with both archives. The packages are development builds:

```text
350b6ccfe262e9b52f1e45a5587473eb8ef81ebf2313ef95a436649f4278bebe  uberclicker-config-v2-debug.apk
730b33bfbca4d448e5a0083722e0a3fb7a3699d7dc40073cdabb56cc53e873ed  uberclicker-config-v2-ios-simulator.zip
```

See `app/artifacts/build-evidence-v2.json` for local sizes/hashes. The simulator
archive is not an installable signed iPhone app. Native BLE, secure-storage behavior
on physical phones and signed/store distribution remain unverified.

## Acceptance limits

The [minimal TX preview](../device-preview.html) awaits human review. The
[dock checks](../cad/exports/validation.json) cover mesh/parameters/sampled cable
clearance; device/connector measurements, retention, internal bridging, printing
and actual charging remain pending. Browser USB writes, physical RF/BLE/HID/MIDI,
relay-free remote configuration, maximum-capacity NVS power cuts and OTA rollback
still need [hardware acceptance](hardware-validation.md).

BLE-only configuration satisfies the current scope; no backend, LAN endpoint,
mDNS discovery or setup hotspot is required. Regional radio selection remains
unimplemented (917 MHz candidate). Native USB HID/MIDI wiring is unverified.
The custom domain is preserved; trusted HTTPS certificate provisioning is still
pending in [deployment](deployment.md). No TLS warning was bypassed.

## Published experimental candidate

[v0.2.0-experimental.1](https://github.com/Baralabite/uberclicker/releases/tag/v0.2.0-experimental.1)
is a public prerelease from clean source commit
`d4d2f2ea37451bb7bd959476961517a79fd994f8`.
[The full Linux build/release run](https://github.com/Baralabite/uberclicker/actions/runs/40850641373)
and [Pages mirror deployment](https://github.com/Baralabite/uberclicker/actions/runs/40851345405)
passed. GCC's misleading-indentation warnings were corrected by expanding control
flow; the radio/output and 69-frame checks remained unchanged.

The published application is 1,574,016 bytes. Its manifest passes the strict release
schema, all four image validations and source-commit/clean checks. Downloaded release
checksums and the corresponding-source ZIP CRC pass. The Pages index's manifest
hash, mirrored manifest and all four images exactly match the GitHub downloads;
the OTA feed selects this compatible two-slot release. Stable remains unavailable.
Native development packages, separate app checksums/evidence/notices and licenses
are attached to the same prerelease. No hardware was flashed.
