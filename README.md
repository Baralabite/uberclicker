# UberClicker

**Experimental:** [v0.2.0-experimental.1](https://github.com/Baralabite/uberclicker/releases/tag/v0.2.0-experimental.1)
contains the multi-receiver firmware, browser/native app builds and verified
installation assets. The simplified TX interface and software checks are complete. Device UI review and physical hardware
acceptance remain pending. No device has been flashed during this work.

An open-source presentation controller for the Elecrow ThinkNode M5, with a
LoRa transmitter managing up to eight receivers, plus standalone BLE keyboard, BLE MIDI, OSC/UDP,
raw UDP and raw TCP outputs. The implementation is **experimental**: it builds
and has host checks, but no device has been flashed or hardware-validated during
this work. Device UI review and physical acceptance are required before stable firmware.

UberClicker's authored source is [MIT licensed](LICENSE). This project operates
noncommercially; MIT permits commercial reuse. The linked firmware includes
GPLv3 GxEPD2 and ships under GPLv3 terms with source and dependency notices.
See [dependency licenses](docs/dependencies.md).

- [Website](https://uberclicker.com/), [browser setup app](https://uberclicker.com/app/),
  [USB installer](https://uberclicker.com/flash/) and [GitHub releases](https://github.com/Baralabite/uberclicker/releases)
- [Usage, claiming, pairing and each output](docs/user-guide.md)
- [Installation, opt-in Wi-Fi OTA and recovery](docs/flashing.md), [app builds and transport limits](docs/app.md)
- [Device UI review](device-preview.html), [actual firmware screens](designs/firmware/review-sheet.png),
  [interface details](docs/device-interface.md)
- [Editable charging dock, STLs and measurement guide](docs/charging-dock.md)
- [Hardware acceptance checklist](docs/hardware-validation.md), [firmware](docs/firmware.md),
  [output formats](docs/outputs.md), [shared protocol](contracts/config-protocol.md)
- [Combined build/test evidence](docs/integration-checks.md)
- [Regional hardware buying links](docs/buying-links.md)

The custom domain is preserved. GitHub confirms its DNS is valid; HTTPS certificate
issuance is still pending. Hosted Web Bluetooth and Web Serial require HTTPS;
localhost is available for development. See [deployment evidence](docs/deployment.md).

| Area | Current status |
| --- | --- |
| Firmware | Public PlatformIO build; authenticated radio sessions/replay protection; no GPS/tracker/private compile-time keys |
| Configuration | Authenticated encrypted BLE to Tx, encrypted LoRa editing of individually keyed Rx, separate cue/admin targets, durable Apply-result recovery and physical reset |
| Apps | Shared browser/native UI; real Android debug and unsigned iOS simulator builds; physical mobile BLE remains untested |
| Releases/installer | CI candidates, explicit prerelease tags, build-derived manifests, verified mirror, BLE-requested, physically confirmed Wi-Fi OTA and pinned browser flasher; real flash/recovery untested |
| Device UI | Large TX home, fresh authenticated per-Rx health, three-item menu; 69 firmware review frames; user/panel review pending |
| Dock | Editable one/two-bay prototypes, rounded walls, 25% wider base, opposing cable tabs and fit coupon; printing/fit/charging pending |

Receiver online/signal status uses fresh authenticated probes, never stored pairing
as proof of connectivity. At SF12, eight-receiver polling takes several minutes;
see [firmware timing](docs/firmware.md). Routine remote configuration does not need
physical Rx setup after initial authorized pairing. Migrated cue-only pairs need
physical administration approval on both devices.

Known limits: one BLE connection at a time; **BLE-only configuration**. Protected
LAN configuration, mDNS discovery and setup hotspot are not implemented. A manual
HTTPS client does not make the device serve HTTPS. There is no account inventory
or remote relay. Radio presets use **917 MHz** without regional selection.
Native USB HID/MIDI is unavailable; CH340 serial programming does not establish
native USB wiring. Profiles are selected in the app. Signed iPhone distribution
and app-store publication require signing/account work and are not included.

Build locally (no device access):

```sh
python3 -m pip install platformio==6.1.19
pio run -d firmware -e thinknode_m5
npm ci --prefix app
npm test --prefix app
node scripts/build-website.mjs
python3 -m http.server 8000 --directory website/dist
```

Open [localhost](http://localhost:8000). Release packaging and the build command
do not upload to a device. [The execution plan](docs/implementation-plan.md) and
[original product targets](docs/product-plan.md) retain the broader requirements;
the status above and acceptance checklist describe what actually exists.
The separate `../gathermesh-lora` project is a read-only reference and is not
included or modified.
