# UberClicker mobile remote

The Capacitor iOS and Android apps use the same `app/src` frontend as the website
and Tauri companion. Phones send Previous and Next to a Cloud Channel;
keyboard and OSC output run on that computer, not on the phone. USB configuration
requires the desktop companion or desktop Chrome.

The internal capability map in `app/src/capabilities.ts` controls navigation,
mounted screens and deep links. iOS and Android show only **Cloud** and
**Cue**. **Configure** is available only through native desktop
USB or desktop Chrome exposing Web Serial; **Listen** is desktop-only.
Mobile web also hides Configure. Other desktop browsers show setup disabled with a compatibility note.
Unsupported mobile deep links return to an available screen rather than exposing an
unusable device editor.

## Pair and use

1. Create a channel in the account dashboard and activate its Pro access.
   Each account can manage multiple channels. Billing is per channel,
   not per phone or hardware clicker.
2. Connect the desktop companion with the channel's Client Companion code, configure
   keyboard/OSC mappings, and enable output.
3. Open **Cue**. The screen automatically displays a pairing code. Enter it in the portal's **Cue Remote → Add** form.
4. Tap the clicker in the portal table, tick its Cloud Channels, and save.
5. Once linked, the phone shows only white Previous/Next arrows on a blue background. The large Forward arrow is centered, with a small Back arrow at the bottom and navigation tabs above. The layout supports portrait and landscape; supported devices provide light tap haptics. Dim arrows indicate unavailable output. Connection failures never queue or retry clicks.

As an alternative to entering a code, choose **Copy link** beside a channel in the portal and open the link on the sending browser. It links that browser clicker to the account and that channel, then opens the remote. Each signed link registers one clicker and expires after ten minutes.

The remote reconnects automatically while visible. Account management stays in the portal, with profile editing, Billing and Sign out in the top-right avatar. Previously saved connections are retained; the selected connection remains active. The shared logo, header and tabs remain above the remote; on desktop the header and tabs stay visible while pages scroll.

Native mobile stores codes in iOS Keychain / Android secure storage. iCloud
synchronization is disabled, and iOS entries use WhenUnlockedThisDeviceOnly.
Browser and desktop sender controls keep imported codes only in memory for that
session. Unlink a clicker from its portal dialog. The phone receives the unlink
notification immediately if connected, or when it reconnects after being offline.
Choose **Retry** to get a fresh pairing code, then link it again in the portal.
The mobile apps do not use Bluetooth or local-network configuration permissions.

Going offline or putting the app in the background disconnects the sender and
invalidates pending clicks. Returning to the foreground reconnects the selected
channel; past clicks are never retried. An unconfirmed result means the click
may have reached the computer: check the channel before clicking again.
Submitted means the computer's output API accepted it, not that presentation
software changed slides. The phone needs to remain awake and in the foreground.

## Transport and idle usage

The shared sender opens `wss://api.uberclicker.com/v1/rooms/{room}/tx` with
subprotocols `uberclicker.v1` and `auth.{txToken}`. The server authenticates the
upgrade headers and selects only `uberclicker.v1`; credentials never enter URLs.
The existing relay protocol provides per-connection sessions, increasing sequence
numbers, 750 ms freshness and correlated results. A sender allows only one pending
click and never buffers offline clicks.

There is no periodic clock polling. Heartbeats use the relay's hibernation-safe
auto-response. A real click after more than five minutes refreshes the server
clock before dispatch; its original tap time still bounds its lifetime. Multiple
idle senders do not keep the room awake. The relay's existing ten-minute window
after the latest real click remains unchanged.

## Build

From the repository root:

```sh
npm ci --prefix app
npm test --prefix app
npm run build --prefix app
npm run sync --prefix app
```

For Android, install Android SDK 36 and a compatible Java 21 JDK, then run
`./gradlew --no-daemon assembleDebug` in `app/android`. The installable debug APK
is `app/android/app/build/outputs/apk/debug/app-debug.apk`.

For iOS, use Xcode and open `app/ios/App/App.xcodeproj`, or build the simulator:

```sh
cd app
xcodebuild -project ios/App/App.xcodeproj -scheme App -configuration Debug \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath artifacts/ios-simulator CODE_SIGNING_ALLOWED=NO build
```

The simulator bundle is under `app/artifacts/ios-simulator/Build/Products/Debug-iphonesimulator/App.app`.
Installation on a physical iPhone requires the owner's Apple signing/provisioning.
Store releases require release signing and distribution work. The mobile CI
workflow builds Android debug and iOS simulator artifacts without publishing.
