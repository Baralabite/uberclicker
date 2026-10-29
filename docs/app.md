# UberClicker app

Implemented development client, 29 October 2026. Configuration schema/logical
protocol 2 supersedes the initial single-pair implementation. Plain TypeScript and Capacitor
8.6.0 share the same UI and protocol client on iOS, Android and supported browsers.
BLE uses `@capacitor-community/bluetooth-le` 8.3.0. There is no account, relay or
server registration. Builds and host tests are not hardware acceptance evidence.

## Run and build

From the repository root:

```sh
cd app
npm ci
npm test
npm run build
npm run dev
```

Open the printed localhost URL. Browser BLE needs a secure context, explicit
user selection and Web Bluetooth support. Stock iPhone Safari/Chrome do not supply
this transport: the page explains the native iOS app alternative. Do not present
the simulator `.app` as an installable signed iPhone distribution.

For native projects, after each web build:

```sh
npx cap sync
cd android
JAVA_HOME='/Applications/Android Studio.app/Contents/jbr/Contents/Home' \
  ANDROID_HOME="$HOME/Library/Android/sdk" ./gradlew assembleDebug --no-daemon
cd ..
xcodebuild -project ios/App/App.xcodeproj -scheme App -configuration Debug \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath artifacts/ios CODE_SIGNING_ALLOWED=NO build
```

Android needs JDK 21 (the Android Studio runtime used here), SDK 36 and network
access for initial Gradle dependencies. A host default JDK 25 failed Gradle's
semantic analysis; using the bundled Android Studio JDK fixed it. Xcode 26.3
successfully built the unsigned arm64/x86_64 simulator app with Swift Package
Manager. The generated projects, lockfile and build wrapper are checked in;
build outputs, caches, local SDK paths and native copied web bundles are ignored.

Local artifacts (untracked; v1 binaries are superseded by reviewed v2 builds):

- `app/artifacts/android/uberclicker-config-v2-debug.apk`: real Android debug APK
  copied from android/app/build/outputs/apk/debug after rebuilding v2.
- `app/artifacts/ios/Build/Products/Debug-iphonesimulator/App.app`: real unsigned
  simulator app, not a signed physical iPhone app or App Store package.
- `app/dist/`: browser distribution; release owner can copy these exact assets.
- `app/artifacts/review-v2/`: multi-receiver UI review screenshots using labelled
  fixture data, not a hardware session.

## Claim and configure

Open physical setup: **Pair new RX** on Tx, **App setup** on Rx. Keep the physical setup window open. Find nearby
clicker invokes the OS/browser chooser; match the device and enter the six-digit
passkey shown on the e-paper screen when the OS asks to pair. The firmware requires
MITM-authenticated encrypted BLE. An old Just Works keyboard bond may need removal
and pairing again; it does not grant configuration access.

For an unclaimed device, enter its displayed code in the app and choose Claim.
The owner token is saved only by the native platform secure-storage plugin:
Keychain with device-only unlocked access on iOS, Android Keystore AES-GCM on
Android. Backups are disabled on Android. The browser keeps tokens only in memory;
it never calls the secure-storage plugin's unencrypted web fallback. Losing a
browser tab's token requires another already-authorized app to revoke ownership,
or a physical factory reset before claiming again. No token/password appears in
ordinary settings reads. Development firmware NVS does not promise confidentiality
against someone extracting flash; production flash encryption remains a deployment
hardening task.

Edit settings pauses cue output. Configure role/route, up to four named profiles,
active profile, Previous/Next keyboard/MIDI/OSC/raw mappings and Wi-Fi. Password
actions are explicit Keep / Replace / Clear. Apply first validates the full draft,
then submits the expected revision for atomic persistence. Cancel discards it. Clicks remain paused while the physical setup window is
open; leave setup on the clicker, or let the window expire, to resume operation.
Disconnect and expiry discard staged state on the device. An uncertain Apply
response requires reconnect/reread; the app never automatically retries it.

Keyboard fields use HID usage codes and modifier bit masks. Default 80/79 are
Left/Right. MIDI offers note, CC and program with channel 1..16 and values 0..127.
OSC uses an address with one int32 value; TCP appends LF to the UTF-8 payload.
Only the selected output is emitted. Delivery/application effects remain distinct
from local submission.

## One transmitter, multiple receivers

A Tx manages up to eight paired Rx devices. Open setup physically on both devices
only for initial pairing. Connect the app to Tx, choose Add a receiver, enter the
Rx identity and a name, then configure the first side. Connect/claim/authorize the
new Rx physically and complete the pair. Repeat for additional receivers: existing
members remain in Tx's `peers[]`, and every relationship gets a different random
key and pair ID. A receiver has only one controlling Tx. An already-managed Rx
requires physical recovery before another controller can take ownership.

After pairing, connect only to Tx. Its Receivers list has a Configure action for
each receiver. The app requests status/settings from the selected Rx over the
actual encrypted LoRa management transport through Tx. A name in the list is stored
membership, not an online claim. An unavailable receiver reports failure; the app
does not display a cached configuration as current. No receiver button or physical
setup window is required for routine remote editing.

Choose Configure, edit that Rx's profiles/output mappings/Wi-Fi, and Apply or
Cancel. Its independent revision guards stale changes. The screen names the Rx ID
and the Tx intermediary; Apply confirms only that target after rereading it.
Changing the selected configuration target never changes `activePeer`, the cue
receiver. Use for cues is a separate explicit local Tx setting. No cue broadcast
or Apply-all is inferred. Remote writes cannot replace the controlling Tx/key,
change the Rx role or radio preset. Only physical recovery can replace the controller; factory reset revokes its
authority and clears the relationship keys.

Management uses authenticated encryption because Wi-Fi passwords must remain
confidential over LoRa. The app persists a pending transaction ID securely before Apply and waits for
actual correlated replies. If its reply is lost, Check previous Apply result
queries the receiver’s atomic durable receipt, including after Rx reboot, then
rereads current settings. Exact duplicates return the recorded revision without
a second commit; conflicting ID reuse fails. Missing receipts remain unknown.
Discard unresolved lookup only clears local metadata; it never undoes a change.
No uncertain Apply is automatically replayed. It disables competing operations while a request is pending;
Disconnect remains available. Disconnect schedules best-effort remote Cancel;
receiver edit expiry is the cleanup backstop. Both Tx and Rx use 120-second idle
leases renewed only by authenticated fresh accepted progress, not duplicates.
Valid fragmented uploads can last longer while advancing. Disconnect restores
normal short Tx setup timeout; a lost Begin/Cancel still resumes Rx by the idle
bound. Transfers have shorter airtime/size-derived deadlines and 3600-second absolute
maximum, not an hour-long idle pause. SF12 configuration can take longer than BLE. The UI makes no instantaneous or hardware-tested claim.

Pairing material remains only in app memory while pairing is incomplete. A failed
first Apply can be reread to establish whether its matching pair ID exists before
proceeding; closing the app discards the key and requires restarting both sides.
Removing one receiver from Tx clears only its key/membership and preserves others.
Recover/reset or re-pair the removed Rx before assigning it another controller.

Schema-1 single-peer persistence migrates to `peers[0]`/`activePeer:0` while retaining
Wi-Fi, owner and cue keys, with adminAuthorized:false. An owner must explicitly
approve remote administration over physical authenticated BLE on each side; a
firmware upgrade never expands old cue authority. The next complete Apply writes schema 2 atomically;
interrupted migration commits preserve the old valid record. No shared group key or automatic data wipe is introduced. Unsupported stored
schemas lock output/configuration for physical recovery instead of becoming an
unclaimed device; the app cannot claim over or silently erase such records.

## LAN and recovery limits

The native app includes a manual HTTPS JSON transport with system certificate
validation. No HTTP credentials, cleartext exception, self-signed-certificate
bypass or hosted-browser HTTP proxy exists. This development M5 firmware advertises
`lan_config:false` and `setup_hotspot:false`; it does not yet supply the required
TLS endpoint, mDNS or a WPA2 setup-hotspot page. These are optional future paths, not required deliverables: native BLE setup
is sufficient and uses no backend/account. The UI names this limitation. BLE setup is the implemented
local provisioning/recovery route. Failed Wi-Fi station connection does not close
BLE setup; reopen it physically to change credentials.

## Checks and review

`npm test` covers the shared JSON Schema fixture, exact cross-language frame bytes,
fragmentation at multiple ATT sizes, idle/total deadlines, UTF-8, malformed/oversized/reordered/duplicate
messages, correlation/session behavior, validate-before-Apply, no automatic retries,
Cancel and client disconnect recovery. `firmware/tests/config/run.sh` executes the
actual service source with host BLE/NVS/clock adapters and OpenSSL SHA-256: owner
authorization, redaction, stale revisions, atomic staging/Apply, failed/torn NVS
writes/reboot selection, expiry/disconnect, reset, revocation and rate limits.
Host tests need a C++17 compiler and OpenSSL 3 (pkg-config or Homebrew); run the firmware build once
to install pinned ArduinoJson. `pio run -d firmware -e thinknode_m5` compiles the
real service against the actual ESP32 Arduino/NimBLE stack.

UI review URLs `?preview=home`, `?preview=edit` and `?preview=receiver` and `?preview=receipt` use clearly labelled static
multi-receiver fixtures with all device actions disabled. They cannot claim/send/configure
hardware. Reviewed at desktop and 320px/390px mobile widths, including text fit and
horizontal overflow. Mobile/desktop screenshots are local build artifacts.

Remaining physical checks: BLE permissions, chooser/bonding/MITM, HID/MIDI/config
coexistence, notification backpressure/reconnect, station provisioning failure,
power cuts/reset, pair-two-devices and RF/output behavior. No device was flashed.
Signed iOS distribution and store delivery need the user's signing accounts.

Project MIT license is in `app/LICENSE`; dependency licenses ship in
`app/public/third-party-notices.txt`, including the native runtime notices and
KeychainSwift license in `app/public/native-notices.txt`. Dependency versions are pinned by package
and native lockfiles.

## Open the firmware-update screen

While directly connected and authorized in physical setup, finish/cancel any edit
and select **Open update screen**. This local-only request opens the clicker's OTA
screen and closes BLE setup. Checking the feed and installing still require the
clicker's physical buttons. Receiver administration through Tx cannot open this
screen. See [installation and recovery](flashing.md) for layout migration and OTA
acceptance limits.
