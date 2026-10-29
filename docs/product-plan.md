# UberClicker product plan

Original delivery targets, 29 October 2026. See the [current implementation status](../README.md)
for experimental firmware, apps, releases and dock prototypes, and the
[hardware acceptance record](hardware-validation.md) for the remaining gates.
This is a delivery plan, not a feature availability statement. UberClicker's authored
source is MIT; linked firmware also carries [dependency license terms](dependencies.md).
The separate `gathermesh-lora` reference remains untouched and is not included here.

## Product decisions for review

The ThinkNode M5 has a monochrome 200×200 e-paper display with an approximately two-second full refresh, according to the [Elecrow manual](https://www.elecrow.com/download/product/ILM13205D/ThinkNode_M5_User_Manual.pdf). Use release-based actions on its two buttons and a deliberate two-button chord for menus. Prefer the quiet action-first transmitter screen, status-first receiver screen, and a split action variant for review. Detailed interaction behavior belongs in [device-interface.md](device-interface.md).

Remove GPS from UberClicker firmware. Avoid per-click display animations, invented slide numbers, and a persistent “connected” claim inferred from an old packet. A LoRa ACK means historical receipt by the peer; a timeout leaves the outcome unknown. An ACK does not establish that presentation software changed slides. UDP can report “Sent,” not “Delivered.”

Keep display refresh independent from click/radio handling. Measure button-to-output latency while the display refreshes. Benchmark new radio presets on hardware before publishing latency or range claims.

## Delivery gates

| Gate | Deliverable | Completion evidence |
| --- | --- | --- |
| 1 — design review | Device screen/interaction prototype, configuration architecture, marketing website with a real 3D M5 hero responding to the mouse | Public website deployed and verified. Device interface approval remains pending |
| 2 — working device | Clean firmware without GPS; Tx/Rx roles; release/chord input; receiver BLE keyboard; standalone Tx OSC/MIDI/UDP/TCP; staged settings and reset | Hardware tests cover input timing, radio receipt/timeout behavior, reconnects, output failures, and click handling during display refresh |
| 3 — configuration | Native iOS/Android app using BLE and LAN; browser app for supported browsers; device-hosted fallback; optional account inventory | Shared settings contract, authenticated writes, Apply/Cancel, provisioning recovery, ownership/revocation/reset tests |
| 4 — distribution | Public repository created; firmware CI, versioned firmware releases, browser USB flasher and user documentation pending | Reproducible CI builds, board-specific release assets/manifest, hashes, clean-device flash/recovery test, actual public links |
| 5 — charging dock | Parametric OpenSCAD dock with USB connection through its base | Measured M5 fit, USB-C alignment, insertion clearance, retention, cable strain relief, print and charge test |

After device design approval, firmware/app work, release/flasher/documentation work, and measured dock CAD can proceed in parallel. The public repository and verified website are live. Website CI does not build firmware. Firmware, apps, releases, a functional flasher, and dock CAD remain pending.

## Engineering constraints

- The existing one-byte, channel-based LoRa protocol lacks device identity and authentication. A shared channel is not secure pairing. Define authenticated identities, key exchange/physical pairing, replay handling, and duplicate handling before shipping secure pairing claims.
- Receiver BLE keyboard behavior exists in the reference source. Native USB HID remains unvalidated: the source README identifies a CH340 serial interface, while the former GPS wiring uses GPIO19/20. ESP32-S3 native USB requires these pins to reach the USB bus, per [Espressif](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-reference/peripherals/usb_device.html). Inspect the actual board before promising USB keyboard support; a wiring or hardware change may be required.
- OSC, MIDI, UDP, and TCP are planned output choices. Define supported transports, message/key mappings, endpoint validation, reconnect policy, and receipt semantics before documenting them as usable.
- The dock preview must be labeled a concept until the physical USB-C port position, connector tolerances, insertion path, retention, and strain relief have been measured and tested. Do not infer manufacturing readiness from a render.

## Release and flasher contract

CI should build each supported board configuration from a version tag, record the source commit and build version, and attach binaries plus a machine-readable manifest to that release. The manifest must identify the board/chip, image offsets and filenames, sizes and SHA-256 hashes, and compatible firmware/settings versions. A hash verifies integrity against the manifest; it does not authenticate an untrusted manifest.

The browser flasher must use the selected release manifest, clearly identify the supported M5 board, check detectable chip compatibility, verify downloaded image hashes, explain erase/settings consequences, and expose recovery instructions. Where hardware identity cannot be detected, require explicit board selection rather than suggesting automatic board verification. Validate the USB serial/bootloader path on real M5 hardware. Browser and driver requirements belong next to the flash action. No release assets or functional flasher exist at this milestone.

User documentation must cover flashing/recovery, pairing, Tx/Rx setup, all standalone output modes, configuration paths and browser limits, radio receipt semantics, factory reset, and dock printing/assembly after validation.

## Publication

The public repository is [Baralabite/uberclicker](https://github.com/Baralabite/uberclicker). The approved website is deployed on GitHub Pages, with production verification completed on 29 October 2026 before custom-domain hookup. This project operates noncommercially and is MIT licensed. The user confirmed preserving `uberclicker.com` in Pages settings; authoritative DNS points to GitHub, while DNS propagation and HTTPS remain pending. The [website address](https://uberclicker.com/) may remain unavailable until those finish. See [deployment.md](deployment.md) for verification evidence, website CI and domain setup.
