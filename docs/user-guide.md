# Using UberClicker

This guide describes the experimental implementation as of 29 October 2026.
Firmware and local protocol/output tests exist; physical device acceptance is
pending. A successful build, app simulator or UI preview is not proof that a
presentation received a cue. Do a rehearsal with the actual host application.

## Install and recover

Use the [installation and recovery guide](flashing.md) for the ThinkNode M5
ESP32-S3, 4 MB, CH340 programming connection and explicit experimental releases.
The browser installer refuses missing releases and incompatible images; use
actual versioned assets. Keep settings-preserving upgrades selected unless you
intend to erase local credentials, profiles and pairing. Do not mix binary
versions or guess flash offsets. If a write is interrupted, reconnect using the
manufacturer's bootloader procedure and reinstall a verified package.

No device was flashed during this implementation. Stable release depends on
[hardware acceptance](hardware-validation.md) and device UI review. A slow or
retained e-paper image alone cannot diagnose a failed install or power state.
Use a known USB data cable, close serial monitors, and inspect the CH340 port if
the installer cannot connect. USB programming does not provide USB keyboard/MIDI.

## Claim and edit nearby

1. On an unclaimed device, initial setup opens automatically. Later, press both
   buttons, release both, choose **Pair new RX** on Tx (or **App setup** on Rx)
   using B1/B2. Setup is a physical
   two-minute local window and pauses all cue output. Authorized remote management
   uses a separate bounded service lease; do not assume the local timer applies.
2. Open the native UberClicker app and choose **Find nearby clicker**. Select your
   device, accept its Bluetooth pairing prompt, and enter the actual six-digit
   code shown on its screen. The device uses that code for authenticated BLE
   pairing. A previous unauthenticated Just Works bond must be removed/re-paired.
3. For a new device, use **Claim clicker** and the displayed code. Bluetooth
   pairing and app ownership are separate checks. The native app stores the
   returned owner token in platform secure storage. Later sessions authorize
   with the saved token and still require a physical setup window.
4. Choose **Edit settings**. Select `tx` or `rx`, route, active profile and output.
   There are up to four named profiles with separate Previous/Next mappings.
   The screen shows **Cues paused** during setup/editing.
5. **Apply changes** validates the complete draft and commits it atomically with
   its expected revision. **Cancel** discards the draft. Setup/edit expiry or
   disconnect discards uncommitted edits. Close setup on the device before using
   cues; the setup window itself keeps output paused after Apply, Cancel or client
   disconnect, until physical exit or the applicable setup lease ends. A remote
   receiver may remain paused until its cancellation is confirmed or lease expires.

Wi-Fi password action is explicitly **keep**, **replace**, or **clear**. A blank
password field is not an instruction to erase a stored password. Stored Wi-Fi,
owner and radio secrets are never read back in ordinary configuration. Applying
Wi-Fi settings does not prove Wi-Fi connected. If join fails, reopen physical BLE
setup and correct the network; local BLE recovery remains available.

If Apply times out, reconnect and reread revision/settings before deciding what
happened. Do not automatically replay Apply or a cue. A stale revision requires
rereading and a fresh edit; an expired edit cannot be resumed.

For app building, platform artifacts and detailed transport behavior, see
[the app guide](app.md) and [configuration protocol](configuration-protocol.md).
A simulator cannot use real iPhone Bluetooth hardware.

## Minimal TX screen and receiver status

TX home shows a large **TX** and only a compact battery/signal/receiver status
bar. **AVG** averages valid fresh RSSI from currently confirmed paired receivers;
`n/n RX` is recently connected / paired. An explicit missing-receiver warning is
separate, so a strong average does not hide an offline receiver. A question mark
means unknown/unavailable, not a fake zero or a connection claim.

Both opens exactly **Receiver status**, **Pair new RX**, **Factory reset**. B1 moves
and B2 selects. Receiver status lists three receivers per page with current RSSI,
Offline or Unknown; B1 changes page, B2 goes back, Both returns home. Pair new RX
opens the native BLE authorization window for adding or managing receivers. There
are no routine profile, route, account or firmware-update menu entries.

A connected count means an authenticated radio response within core's freshness
window. It does not prove a cue arrived or a presentation advanced. Offline means
not recently confirmed, including a missed response, busy radio or interference;
it does not prove power is off. Stale/unpaired readings never contribute to the
count or average. The battery is coarse and uncalibrated, with no percentage or
charging inference. The quiet TX home intentionally omits last-action/ACK messages;
verify the actual receiving application during rehearsal.

## Add receivers to one transmitter

Use a matching protocol-v2 app and firmware. A Tx supports up to eight receivers,
with a separate key for each; an Rx accepts at most one controller. Initial
pairing is the direct physical setup step on **both** devices:

1. Authorize the Tx in the app. Open **Add a receiver**, enter a friendly name and
   the receiver's 12-character device ID, then **Configure first device**. This
   appends a relationship; it does not replace the Tx's other receivers.
2. Keep the app open, disconnect and physically authorize the intended Rx. Choose
   **Complete on this device**. The Rx gets the matching controller relationship.
3. Pairing stays incomplete until both Apply operations succeed. If a reply is
   lost, reconnect and reread that device before proceeding. A locally queued
   request is not evidence of successful pairing.
4. Repeat for each additional Rx. Close physical setup and test the intended
   receiver's actual output. Stored keys are not readable through settings.

The app holds temporary pair material only in memory. If you close it or discard
that material halfway through, restart the incomplete pairing deliberately. An
Rx already assigned to a different controller needs physical recovery first.
**Pair new RX** opens local native-app access; it does not add a receiver on its own.

The app's **Receivers** panel gives each paired Rx its own **Configure**,
**Use for cues** and **Remove** actions. **Configure** chooses an administrator
target. **Use for cues** explicitly changes the saved `activePeer` cue destination
on Tx. These are independent: inspecting Stage right does not redirect cues away
from Stage left. There is no implicit cue fanout or Apply-all operation.

A migrated older pairing preserves its cue key but starts as **Cue pairing only**
(`adminAuthorized:false`). It does not silently acquire permission to configure a
receiver. Physically open setup and authorize **each** side in turn, then use
**Approve remote administration** for that relationship on both Tx and Rx. New
physical pairings grant this authority explicitly. A one-sided grant is incomplete;
Configure remains unavailable or the receiver rejects it until both sides approve.

## Configure a receiver through the transmitter

The revised v2 workflow below requires `remote_config` capability from the working
encrypted radio bridge. App/config and radio integration are being revised; final
combined-build and physical evidence must be recorded before claiming end-to-end
acceptance. A disabled Configure button means the connected firmware does not
provide this capability. The review images use demonstration status only.

1. Open **Pair new RX** on **Tx**, authorize the app, then choose **Configure**
   on the intended receiver row. Ordinary remote work does not require opening
   setup or pressing buttons on that Rx again after initial pairing.
2. Check the target receiver name and full device ID in the app/device screen.
   Wait for its own status and configuration response. Cached status is historical;
   it is not evidence that the Rx is reachable now. Long-range LoRa configuration
   can take minutes; a pending request has no confirmed result yet.
3. Choose **Edit settings** for that target. Change its profiles, active profile,
   output mappings/endpoints or Wi-Fi. Its role, controller/key and radio preset
   cannot be changed remotely, because that could strand the management path.
   The receiver displays **Managed by TX / Cues paused** while its edit is active.
4. **Apply changes** validates and commits only this Rx, using its own revision.
   Treat it as successful only after that receiver's matching successful Apply
   response. A success on another Rx says nothing about this one.
5. **Cancel** discards the target draft when confirmed. **Back to transmitter**
   returns to Tx settings. Leave Tx setup when finished; it remains paused during
   management. Cancellation does not undo an already committed Apply.

A timeout, disconnect or lost Apply reply is an **unknown outcome**, even if the
receiver applied the change. Reconnect through Tx and select the **same full
receiver ID**, then choose **Check previous Apply result**. The app keeps a pending
transaction identifier; the receiver looks for its durable receipt, including
after reboot. A matching `applied` result confirms the saved revision and the app
rereads current settings. A successful lookup returning `unknown` is **not** an
Apply confirmation. It may mean no matching receipt remains in the bounded history.

The receiver stores its last eight receipts atomically with settings and scopes
them to controller/pair/transaction. A repeated matching transaction returns its
saved result; conflicting reuse is rejected. The app does not automatically
replay uncertain Apply. **Discard unresolved lookup** only forgets the lookup
locally; it neither undoes a commit nor sends another Apply. Reread current Rx
settings before deliberately starting another edit. There is no offline edit queue.

Remote edit recovery uses **120 seconds without fresh authenticated progress**.
Accepted new fragments/acknowledgements can renew progress; duplicate/replayed
traffic cannot. The one-hour transfer limit is an absolute ceiling, not a reason
to leave an abandoned receiver paused for an hour. A lost Cancel is best-effort;
idle recovery is the backstop. The screen follows actual service state and does
not promise a precise finish time or a live LoRa connection.

Physical reset/recovery remains available on every RX. During remote editing,
B1/B2 emit no cue; Both opens local menus, including the guarded factory reset.
A failed Wi-Fi join can be corrected over the preserved LoRa control relationship
or local physical BLE setup. Remote configuration requires authenticated encryption
for secrets; the earlier cue-authentication format alone is insufficient.

For ordinary LoRa cues, release B1 for Previous or B2 for Next on Tx. A held
button does not repeat. Press both long enough to debounce, then release both for
Menu; no individual cue escapes the chord. RX buttons navigate local status/menu
and never emit cues. Select the active profile in the native app; there is no profile selector on
the minimal TX screen. Local menus/setup and the edited RX pause
output according to their actual service state.

To remove a relationship, choose **Remove** on that Rx's row. This removes the
Tx's key for that Rx; physically recover/reset or re-pair the former receiver
before giving it a different controller. It does not reset the other receivers.
**Remove app authorization → Revoke owner** clears local owner authorization and
BLE bonds; a new physical claim/pairing is required. Neither action promises a
change on an offline peer.

## Standalone outputs and working examples

Use `tx` + `direct` for a standalone clicker. Use `rx` for the downstream output
of a LoRa pair. In both cases select an output in the active profile. Firmware
supports BLE keyboard, BLE MIDI, OSC/UDP, raw UDP and raw TCP. Native USB HID/MIDI
and RTP-MIDI are unavailable. Output settings and exact encodings are documented
in [outputs](outputs.md).

| Output | Configure and test |
| --- | --- |
| BLE keyboard | `ble_keyboard`, Previous usage 80 / Next 79, modifiers 0. Open physical setup to pair the host OS with the displayed passkey, then exit setup. In a focused slideshow or keyboard event viewer, press/release B2 once for Right Arrow and B1 for Left Arrow. Firmware sends a zero report after each press. |
| BLE MIDI | `ble_midi`; Next type `note`, channel 1, number 60, value 100. Pair during setup, connect in the host's BLE MIDI utility, route to a MIDI monitor, then exit setup. Expect Note On `90 3c 64` followed by Note Off `80 3c 00`; BLE timestamp bytes precede the MIDI bytes. Host routing/release remains a physical test. |
| OSC over UDP | `osc_udp`, host = the computer's LAN IPv4, port 8000, Next OSC `/next`, MIDI value 100. Run the UDP listener below. Expect `2f6e6578740000002c69000000000064`, one OSC int32 argument. |
| Raw UDP | `udp`, same host/port, Next payload `next`. The UDP listener prints bytes `6e657874`; no terminator is added. |
| Raw TCP | `tcp`, same host/port, Next payload `next`. Run the TCP listener below. Expect `6e6578740a`, payload plus LF, and one connection per cue. |

For network outputs, provision Wi-Fi on the same trusted LAN as your computer,
allow the chosen local port through its firewall, and substitute the computer's
address for the profile host. `127.0.0.1` on the M5 means the M5, not your computer.
These output routes have no destination authentication/TLS. Configuration secrets
are provisioned over authenticated BLE. Sending OSC/UDP/TCP on a LAN does not
require a LAN configuration service.

Save this as `udp-listener.py` on the destination computer and run
`python3 udp-listener.py` for either UDP example:

```python
import socket
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
    listener.bind(('0.0.0.0', 8000))
    while True:
        data, peer = listener.recvfrom(512)
        print(peer, repr(data), data.hex(), flush=True)
```

For TCP, save as `tcp-listener.py` and run `python3 tcp-listener.py`:

```python
import socket
with socket.socket() as listener:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('0.0.0.0', 8000))
    listener.listen()
    while True:
        connection, peer = listener.accept()
        with connection:
            connection.settimeout(3)
            data = b''
            while b'\n' not in data and len(data) < 129:
                part = connection.recv(129 - len(data))
                if not part:
                    break
                data += part
            print(peer, repr(data), data.hex(), flush=True)
```

Production codec bytes were checked against actual local UDP/TCP listeners by
`python3 firmware/tests/run_host.py`, including the exact examples above. That
establishes encoding/framing on the host, not Wi-Fi delivery from an M5. Keyboard,
BLE MIDI and receiving application behavior still need physical host testing.

No cue is automatically retried after reconnect. Busy input is rejected rather
than queued. A **Sent** result means local send completion; **Submitted** means an
output API accepted the event; **Received** means a receiver acknowledged it.
None proves the slide advanced. **Unconfirmed** means delivery is unknown; verify
the application before deciding whether to press again.

## Native BLE configuration and browser flashing

The required configuration path is **native iOS/Android app → BLE → Tx →
authenticated encrypted LoRa → paired Rx**. Native BLE is sufficient. There is no
account, backend, hosted configuration page, protected LAN setup or device hotspot
required for this workflow. Initial receiver pairing and physical reset/recovery
remain local actions. Wi-Fi is needed only for selected network outputs or the
separate device firmware-update feature, not for ordinary BLE configuration.

The native app needs the platform's Bluetooth permissions and stores its owner
token in secure platform storage. An optional browser build is a development
convenience, with Web Bluetooth support/secure-context limitations and memory-only
owner tokens. Stock iPhone/iPad browsers do not supply its BLE route; use the native
app. Losing a browser tab's token requires an already-authorized app to revoke
ownership, or physical reset/reclaim if no authorized app remains. This optional
path is not a configuration acceptance gate.

**Browser firmware flashing remains supported and required** as a separate USB
installation/recovery route. Use the [browser installation guide](flashing.md)
with a supported desktop browser and the M5's CH340 serial port. Removing a hosted
configuration requirement does not remove the browser flasher or its image/hash,
board-selection and explicit erase checks. Neither route requires a backend
account. Do not enter owner/Wi-Fi secrets into an unprotected local HTTP page.

## Reset, troubleshooting and charging

If stored settings are unsupported or damaged, the device shows **Recovery** and
blocks claim/configuration and cue output. Both opens **Factory reset / Back**;
no app authorization can bypass this. Reset is deliberate data loss, so it still
starts with Cancel and requires the guarded hold below. The firmware is preserved;
the app never silently erases incompatible settings.

Factory reset: Both → Menu → Factory reset. Cancel starts selected.
B1 selects Erase, B2 arms it, then hold both continuously for three seconds.
Releasing either early cancels without a cue. Core clears local settings, Wi-Fi,
profiles, owner/radio material and BLE bonds, then restarts unclaimed; firmware
remains. A reset request is not a successful wipe until persistence and bond
removal succeed. Remove obsolete host Bluetooth entries before pairing again.

If buttons seem inactive, exit menus/setup and wait for the slow panel refresh.
Check TX/RX role, route, active profile and receiving app focus/subscription. A
receiver's own buttons never generate test cues. For an uncertain cue, check the receiving app before another press. The new
authenticated probes report recent radio reachability, not application success
or guaranteed range. For MIDI notes/keys stuck during disconnect, reconnect to let release
recovery run before another cue. Verify actual host behavior during rehearsal.
A battery bar is coarse and uncalibrated; it is not a runtime or charging promise.

The [charging dock](charging-dock.md) is an unmeasured fit prototype under `cad/`,
with parameterized USB retention and measurement worksheets. Its exports have
not been printed, fitted or charge-tested. Measure your device/plug and print the
fit coupon first. The cradle must support device weight through its floor, not
the USB connector. Use a suitable cable/power supply and verify charging on the
actual hardware; the firmware does not infer charging state.
