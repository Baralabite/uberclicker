# UberClicker

**Current implementation:** one-way radio and USB serial-only setup. Update TX and RX together; older release pairing instructions do not apply. Hardware acceptance is pending.

**Earlier experimental release:** [v0.2.0-experimental.1](https://github.com/Baralabite/uberclicker/releases/tag/v0.2.0-experimental.1)
contains the multi-receiver firmware, browser/native app builds and verified
installation assets. The simplified TX interface and software checks are complete. Device UI review and physical hardware
acceptance remain pending. No device has been flashed during this work.

An open-source presentation controller for the Elecrow ThinkNode M5, with a
one-way LoRa transmitter and receivers that select a recently heard TX, plus per-button action lists combining LoRa, BLE keyboard, BLE MIDI, OSC/UDP,
raw UDP and raw TCP, including multiple network destinations. The implementation is **experimental**: it builds
and has host checks, but no device has been flashed or hardware-validated during
this work. Device UI review and physical acceptance are required before stable firmware.

UberClicker's authored source is [MIT licensed](LICENSE). Local tools remain free; Cloud Channels are an optional paid Pro service. MIT permits commercial reuse. The linked firmware includes
GPLv3 GxEPD2 and ships under GPLv3 terms with source and dependency notices.
See [dependency licenses](docs/dependencies.md).

- [Website](https://uberclicker.com/), [account dashboard](https://app.uberclicker.com/), [browser setup app](https://app.uberclicker.com/#configure),
  [USB installer](https://uberclicker.com/flash/) and [GitHub releases](https://github.com/Baralabite/uberclicker/releases)
- [Direct configuration, RX selection and outputs](docs/user-guide.md)
- [Cloud relay and desktop companion](docs/cloud.md) — TX over Wi-Fi to keyboard/OSC on a computer
- [Phone remote for iOS and Android](docs/mobile.md) — shared UI with Previous/Next cloud controls
- [Installation, opt-in Wi-Fi OTA and recovery](docs/flashing.md), [app builds and transport limits](docs/app.md)
- [Device UI review](device-preview.html), [actual firmware screens](designs/firmware/review-sheet.png),
  [interface details](docs/device-interface.md)
- [Editable charging dock, STLs and measurement guide](docs/charging-dock.md)
- [Hardware acceptance checklist](docs/hardware-validation.md), [firmware](docs/firmware.md),
  [output formats](docs/outputs.md), [shared protocol](contracts/config-protocol.md)
- [Combined build/test evidence](docs/integration-checks.md)
- [Regional hardware buying links](docs/buying-links.md)

The website and account app are deployed on Cloudflare with working HTTPS.
Localhost is available for development. See [deployment evidence](docs/deployment.md).

| Area | Current status |
| --- | --- |
| Firmware | One-way broadcasts, passive RX discovery and selection; no GPS/tracker |
| Configuration | USB serial only on TX and RX; automatically trusted, independent device settings |
| Apps | Shared browser/Tauri UI for USB setup; desktop adds background cloud keyboard/OSC receiving; earlier native mobile builds do not configure current firmware |
| Releases/installer | CI candidates, explicit prerelease tags, build-derived manifests, verified mirror, serial-requested, physically confirmed Wi-Fi OTA and pinned browser flasher; real flash/recovery untested |
| Device UI | TX broadcast status; RX recent-TX list and single-press selection; physical panel review pending |
| Dock | Editable one/two-bay prototypes, rounded walls, 25% wider base, opposing cable tabs and fit coupon; printing/fit/charging pending |

RX never sends radio replies. TX cannot know which receivers heard a cue. Configure each device directly over USB serial; Bluetooth is reserved for keyboard/MIDI output and stays connected when USB is attached. Connecting/reading over serial leaves clicks active; only edits pause cues. No app credentials are stored. The device name identifies each TX on the RX list.

Known limits: one BLE keyboard/MIDI host connection at a time; no BLE configuration. Protected
LAN configuration, mDNS discovery and setup hotspot are not implemented. A manual
HTTPS client does not make the device serve HTTPS. The web dashboard manages Pro Cloud Channels and Stripe billing. Cloud TX uses the authenticated relay and desktop Companion; local outputs remain free.
Radio presets use **917 MHz** without regional selection.
Native USB HID/MIDI is unavailable; CH340 serial programming does not establish
native USB wiring. Profiles are selected in the app. Signed iPhone distribution
and app-store publication require signing/account work and are not included.

Flash a connected device as a transmitter with `./flash.sh tx`, or as a receiver
with `./flash.sh rx`. These preserve compatible settings and select the role in
firmware. Add `--erase` to delete all saved settings before flashing; see
[local flashing](docs/flashing.md#flash-locally-as-tx-or-rx).

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
