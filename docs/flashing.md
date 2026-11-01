# Firmware installation and recovery

The [browser installer](https://uberclicker.com/flash/) accepts only published
experimental ThinkNode M5 releases. Stable firmware is unavailable pending device
UI review and physical hardware acceptance. CI and serial mocks do not establish
a successful hardware flash. Do not flash a connected device without its owner's
approval.

## Desktop Companion installation

Open **Install** in Companion on macOS, Windows or Linux. The native USB bridge
works without browser Web Serial, Python or PlatformIO. Connect a USB data cable,
disconnect any session in **Configure**, then refresh and select the intended USB
device. Confirm the physical board, choose a prerelease and installation mode,
then **Verify downloads & connect**. Review the ready state before **Write firmware**.

Companion reads published firmware releases directly from GitHub, using GitHub's
manifest SHA-256 digest and the image hashes in that manifest. Only public
prereleases with a completed, hashed manifest asset appear. **Refresh releases**
loads new builds or retries after a network error; GitHub's unauthenticated API
rate limit can temporarily prevent loading. The browser installer still uses the
website mirror, which must be deployed separately to Cloudflare production.

The desktop and browser installers use the same pinned flashing engine and safety
checks: ESP32-S3/4 MB detection, exact partition matching for Keep settings,
explicit consent for full erase, and SHA-256 read-back verification. Configure and
Install share exclusive native serial ownership. A failed connection or write
requires an explicit retry; never unplug during writing. No physical flash has
been validated by these software tests.

The installer uses configurable firmware. After installation, use **Configure**
to select Transmitter or Receiver. For a role fixed in firmware, use the release's
`thinknode-m5-tx.zip` or `thinknode-m5-rx.zip` with the command-line recovery
instructions below. Each archive contains a complete, independent image set,
manifest (`firmwareRole: tx` or `rx`), checksums, source and licenses. Extract one
archive and use only its images and offsets. Installing configurable firmware over
a fixed-role build removes the role lock while preserving compatible settings.

## Flash locally as TX or RX

From the repository root, with PlatformIO installed and the intended device
connected by USB:

```sh
./flash.sh tx
./flash.sh rx
```

Each command builds and uploads firmware with that role fixed. RX stays RX after
reboots and factory resets, including when previously saved settings said TX.
The app displays the firmware-selected role and disables changing it. To choose
a specific device, pass its serial port, for example
`./flash.sh rx /dev/cu.wchusbserial210`.

By default the script preserves NVS, including profile names, ownership, Wi-Fi
and compatible pair settings. To start fresh, run `./flash.sh rx --erase` (or
`./flash.sh tx --erase`). The optional serial port can go before or after `--erase`.
This builds first, then erases all flash and uploads firmware. It deletes all
saved settings, including profiles, ownership, Wi-Fi credentials and pair keys.
The fresh device name defaults to the last four hex digits of its Bluetooth MAC.
The selected role is saved atomically with a new settings revision on first boot
when it differs from the stored role. A failed save keeps configuration locked.
Converting a Tx with multiple receivers to Rx requires reducing it to one
relationship first; incompatible saved topology
locks configuration for recovery instead of deleting relationships or loading an
older record. These commands use the current OTA partition layout; an older
single-slot installation needs the explicit migration described below first.
The original `thinknode_m5` environment remains app-configurable.

## Browser installation

Use desktop Chrome or Edge on HTTPS (localhost also works for development), a USB
data cable and the Elecrow ThinkNode M5's CH340 serial connection. Close PlatformIO
monitors and other programs using the port. Safari/iPhone/iPad Web Serial is not
supported. Native USB HID/MIDI is separate from programming and is unverified.

1. Select **Elecrow ThinkNode M5 · ESP32-S3 · 4 MB · AU915 candidate** after checking the physical
   board. Chip detection alone cannot identify its board model or radio variant.
   This candidate uses 917 MHz; regional radio selection is not implemented.
2. Select **Experimental prereleases**, a real published version, and acknowledge
   its acceptance limits. An empty release list means no matching assets exist;
   the installer does not generate example firmware.
3. Leave **Preserve settings** selected for an upgrade. This requires an exact
   match of the installed partition-table bytes. The installer refuses changed
   layouts. Existing firmware from another project generally needs full erase.
4. If deliberately replacing the installation, select **Erase entire device**
   and check the explicit consent box. This destroys firmware, Wi-Fi credentials,
   local authorization, profiles and pair keys. It does not alter an offline cloud
   record. No account/relay service is implemented here.
5. Choose **Verify downloads & connect**, then the M5's CH340 port. All manifest
   and image checks finish before opening serial. Connecting resets the device
   into its bootloader. ESP32-S3 and exactly 4 MB must be detected.
6. Review the ready status and choose **Write firmware**. Keep cable and power
   connected. The installer writes the exact release images, reads them back,
   compares SHA-256 and requests a reset. Test device behavior separately.

Preserve mode checks that image erase sectors avoid NVS. It preserves existing
settings bytes only when the release layout permits; it is not a backup or a
promise that future schema migrations preserve every setting. The current settings
schema is recorded in each release manifest. The installer never retries a write or silently switches to erase.
V2 firmware migrates supported schema-1 records in memory, retaining ownership,
Wi-Fi and the existing single-peer key; the next Apply commits schema 2 atomically.
The manifest separates settings schema 2, logical configuration protocol 2, BLE
framing 1, cue radio 1 and encrypted receiver-management radio 2. Unknown schemas
must not be treated as a settings-preserving upgrade.

## Opt-in Wi-Fi OTA updates

After installing an OTA-capable release by USB, configure Wi-Fi through the app.
Connect the local TX or RX by USB serial in the desktop browser app, then choose
**Open firmware update screen**. Serial is trusted without opening pairing/setup. Finish or
cancel any edit first. This opens only the clicker's update screen; press its
physical **B1 Check** to contact the update feed. The three-item Tx menu stays
unchanged. Opening this screen enables OTA for that session only;
B2 or both buttons exit and disable it. There are no background checks, automatic
installs, open upload ports, or app-triggered firmware writes.

The screen offers the newest published compatible **experimental** release after
its GitHub Pages mirror deploys. Review the version and press **B1 Install** to
confirm; B2 exits without installing. A different version can be older than the
installed version: this is a deliberate manual choice, not a version upgrade
policy. The installed version is shown before checking. A low battery reading
blocks installation; keep reliable power connected throughout. During checking,
installation and restart, buttons cannot dismiss the session and cues remain
paused. Errors return to a retry/exit screen. Configure Wi-Fi first if disconnected;
retry clock synchronization if the network blocks NTP.

**One-time USB migration:** earlier `huge_app.csv` installations have one large app
slot. OTA needs `firmware/partitions.csv`: NVS at 0x9000, OTA data at 0xe000, and
app slots at 0x10000 and 0x200000, each 0x1f0000 bytes. The browser's preserve mode
intentionally refuses this layout change. Use its explicit full-erase installation
flow once, then claim, configure Wi-Fi and pair devices again. Future OTA writes
only the inactive app slot, preserving settings and bonds. It never changes the
bootloader or partition table; schema/protocol/layout changes need USB migration.

GitHub Releases hosts the source and binaries. The existing Pages workflow verifies
the published assets, mirrors them under `flash/releases/<version>/`, and writes
`flash/releases/ota.json` as the newest compatible release manifest. Legacy
single-slot releases stay available in the USB installer but cannot enter the OTA
feed. No compatible release produces an empty feed, not sample firmware. Publishing
an explicit prerelease tag runs this pipeline; CI artifacts alone are not OTA offers.

The device requires verified HTTPS at `https://uberclicker.com`, checks certificates
against the bundled CA store, and synchronizes its clock before certificate checks.
Redirects and insecure HTTP are refused. It verifies manifest board, chip, flash,
settings/config/radio versions and the exact partition layout, then checks image
size, SHA-256, ESP image validity and embedded UberClicker version/project before
selecting the new boot slot. GitHub/Pages publishing access is the trust boundary;
there is no separate offline firmware-signing key.

The previous slot remains selected on download/hash/validation failure. Arduino's
early OTA confirmation is deferred until board, BLE/config/reset-journal and
configured radio initialization finish; a pending image that fails those checks
requests rollback. This is a startup check, not complete runtime health validation.
Power-loss recovery, actual bootloader rollback, available heap while BLE/TLS run,
and physical installation still require the hardware acceptance checklist. HTTPS
certificate issuance and a published compatible release must be available before
live OTA can succeed; TLS verification is never bypassed to work around hosting.

## Recovery and command-line installation

If discovery fails, use a known data cable, close other serial clients and check
OS CH340 support. Reconnect/power-cycle if a cancelled connection leaves the device
in its bootloader. For an interrupted write, reconnect in download/bootloader mode
using the manufacturer's instructions, then reinstall. Do not infer an application
failure solely from slow e-paper refresh. A hash/readback error is a failed install.

Download the manifest, SHA256SUMS and **all** `.bin` assets from one
[GitHub release](https://github.com/Baralabite/uberclicker/releases). Never mix
versions or copy generic ESP32 offsets. After checking SHA256SUMS, use pinned
Espressif esptool (Python package `esptool==5.1.0`):

```sh
python3 -m pip install esptool==5.1.0
python3 -m esptool --chip esp32s3 --port YOUR_CH340_PORT --baud 115200 flash-id
```

Confirm ESP32-S3 and 4 MB. The following prints a write command from the manifest
without running it. Inspect it before deliberate use; this command does not erase
all flash, so use it only with an independently confirmed matching partition table.
For an intentional full replacement, `erase-flash` is a separate destructive step
requiring the device owner's approval.

```sh
python3 - <<'PY'
import hashlib, json, shlex
from pathlib import Path
m = json.loads(Path('manifest.json').read_text())
assert m['board'] == 'thinknode_m5' and m['chip'] == 'esp32s3' and m['flashSize'] == 4194304
command = ['python3', '-m', 'esptool', '--chip', 'esp32s3', '--port', 'YOUR_CH340_PORT', '--baud', '115200', 'write-flash']
for image in m['images']:
    data = Path(image['file']).read_bytes()
    assert len(data) == image['size'] and hashlib.sha256(data).hexdigest() == image['sha256']
    command += [hex(image['offset']), image['file']]
print(shlex.join(command))
PY
```

## Release pipeline

Pull requests, main pushes and manual runs build all three PlatformIO variants:
configurable, fixed TX and fixed RX. Package, host and flasher checks must pass
before downloadable artifacts are retained. Main pushes and manual runs on main
publish a GitHub prerelease tagged `v0.1.0-ci.<run-number>.<run-attempt>`; attempts
use distinct versions to avoid replacing published bytes. An explicit semantic
prerelease tag such as `v0.1.0-experimental.1` also builds and publishes a release.
Pull requests never publish releases. Assets are uploaded to a draft before it
becomes public. The top-level manifest/images are configurable firmware; TX/RX
packages are separate ZIP assets with their own manifests and source.
The version excluding `v` must fit the 31-character ESP application descriptor. Plain
stable tags are rejected until hardware acceptance and a reviewed promotion change.
The manifest records board/chip/flash, source commit, firmware/settings/config/radio
versions, exact offsets from PlatformIO's actual uploader environment, and image
sizes/SHA-256. No factory image with hidden erase behavior is supplied.

Pages reconstructs a same-origin mirror from every published prerelease, checks
each downloaded image, rejects changed bytes at an existing version path, and
records the manifest hash in `flash/releases/index.json`. All prior published
versions remain selectable. GitHub release download CORS is not assumed. Build
failure blocks release; mirror failure blocks deployment. Companion can use a release as soon as it is published. Browser/OTA availability
depends on deploying the mirror: the existing Pages workflow does this for legacy
hosting, while the live Cloudflare website currently requires a separate deployment.

Local checks (no device access):

```sh
npm ci --prefix app
python3 scripts/tests/test-package-release.py
python3 firmware/tests/ota/run.py
node --test website/flash.test.mjs scripts/tests/mirror-releases.test.mjs
python3 scripts/package-release.py --version 0.1.0-local.1 --allow-dirty --output /tmp/uberclicker-candidate
```

The build script uses a fresh temporary build directory and configuration
to embed the candidate version, leaving the developer's PlatformIO file unchanged.
It does not upload. The existing source reference remains outside the public build.
Local dirty builds carry `sourceDirty: true` and cannot enter the published mirror.

## Third-party attribution

The browser bundles [Espressif esptool-js 0.7.0](https://github.com/espressif/esptool-js)
under Apache-2.0, with its license in `website/dist/flash/vendor/`. Its bundled
pako 2.1.0 carries MIT/Zlib attribution. UberClicker remains MIT licensed; third-party
licenses retain their own terms. No manufacturer's firmware or private credentials
are redistributed.

The linked firmware includes GPLv3 GxEPD2 and ships under GPLv3 terms with a
corresponding-source archive. UberClicker's own source retains MIT permissions.
See [dependency notices](dependencies.md) and the source/license assets in each release.
