# Standalone outputs

Implemented in the experimental M5 firmware, compile/host checked. BLE HID/MIDI
and Wi-Fi device behavior require physical acceptance before stable release.
Configure each profile’s **Previous button click** and **Next button click** action
lists. Each event can contain up to eight actions: LoRa TX, BLE keyboard, BLE MIDI,
OSC, UDP, TCP or Cloud TX. Add the same network type multiple
times for independent destinations. Enable, duplicate, reorder or remove actions
in the app. RX runs these lists for received events; its physical buttons select a TX.
LoRa TX broadcasts only from a transmitter. Remove or disable it for local-only clicks.

All routes stop during physical pairing, configuration editing and device menus.
Connecting USB and reading settings leaves the BLE keyboard connected and output
active; Apply/Cancel resumes output after a settings edit.
No output cue is retried or replayed after reconnect. Busy inputs are rejected
without an event queue. Enabled actions within one event run in list order, waiting
for network completion or a BLE release as needed. A failed action does not prevent
later actions from running; a mixture of success and failure reports Unknown.
The whole event expires after 750 ms, so slow destinations can exhaust its budget.
Pause, profile changes and reconnect cancel remaining actions without replay.
Each network action owns its host and port; there are at most 4 profiles. Total
settings and secrets must also fit the existing 8192-byte storage/message limit. Device names are 1..24 UTF-8 bytes, hostname/IPv4
at most 63 ASCII letters/digits/dots/hyphens, and port 1..65535. IPv6 literals and
URL schemes are not accepted. The config service validates complete settings;
codecs enforce wire payload bounds again at submission.

## Cloud TX

Add **Cloud TX** to an event to save the fixed root URL
`https://api.uberclicker.com`. Actions store this as type `cloud`, host
`api.uberclicker.com` and port `443`; HTTPS is implicit for this output.
Cloud TX uses a persistent certificate-verified WebSocket to a private relay room.
Create a room in UberClicker Companion, paste its TX pairing code into **Cloud
connection** in the USB setup app, then add Cloud TX actions and save Wi-Fi.
The sender token is write-only over USB, persists with the atomic configuration
record, and is cleared by removing the connection or factory reset. It is never
returned in status or readable settings. The companion maps Previous/Next to
keyboard and OSC outputs; the TX sends button events rather than desktop commands.

Cloud work runs in its own task. Offline, stale and busy cues are rejected; a cue
is never resent after reconnect. A companion acknowledgement reports Submitted
when the local OS/network API accepted the output, not that the target application
acted. A lost acknowledgement is Unknown and must not trigger a retry. TX needs
NTP once for TLS certificate validation. See [Cloud setup and protocol](cloud.md).

## BLE keyboard

Output `ble_keyboard` sends an 8-byte standard keyboard report containing modifier
bits and one HID usage, then a zero report after at least 30 ms. Keys 0..101 and
modifiers 0..255 match the configuration schema. Default Previous/Next are Left
Arrow (80) and Right Arrow (79). This is BLE HID, not USB HID through CH340.

Open **Bluetooth pairing** on the device, select its displayed name (such as
`UberClicker RX: John Board`) in the host OS, and enter the six-digit passkey. Exit setup before testing clicks. One BLE output-host connection is supported at a time.
Configuration uses USB serial only and can run while the output host stays connected. A connected,
MITM-authenticated, encrypted, subscribed host and successful initial release are required. After
disconnect, the next connection receives zero reports before it can accept a
new press. Failed release remains pending and blocks another key. Success means
the BLE stack accepted the notification, not that PowerPoint or another app acted.
An old Just Works bond must be forgotten/re-paired through physical setup.

## BLE MIDI

Output `ble_midi` exposes MIDI service
`03b80e5a-ede8-4b33-a751-6ce34ec4c700` and characteristic
`7772e5db-3868-4112-a1a9-f2669d106bf3` on the same BLE server. Open **Bluetooth pairing**, pair, then connect using the host's BLE MIDI tool and subscribe to notifications.
Its UUID is included in the active BLE scan response so MIDI service-filtered
host discovery can find it. MIDI OS support/routing still needs host-specific
physical validation.

Each notification contains a BLE MIDI header/timestamp followed by one complete
MIDI message. Timestamp is the low 13 bits of device milliseconds. Configuration
channel 1..16 maps to wire channel 0..15. Number and value are 0..127:

| Type | Press | Release |
| --- | --- | --- |
| `note` | Note On, number, value | Note Off, same channel/number, velocity 0 after ≥30 ms |
| `cc` | Control Change, number, value | No synthetic reset; controller retains value |
| `program` | Program Change, number (zero based) | None; value field ignored |

Example: type `note`, channel 1, number 60, value 100 sends `90 3c 64`, then
`80 3c 00`. Type `cc`, channel 2, number 64, value 127 sends `b1 40 7f`.
Type `program`, channel 3, number 12 sends `c2 0c`. Reconnect/pause requests CC123
All Notes Off on all 16 channels and blocks new MIDI until successful. A lost
note release is retried as a release only. A reconnect never repeats Note On.
BLE MIDI is the selected MIDI transport; USB MIDI and RTP-MIDI are unavailable.
The packet format follows [Silicon Labs' BLE MIDI description](https://docs.silabs.com/bluetooth/2.13/bluetooth-code-examples-applications/midi-over-ble).

## OSC over UDP

Output `osc_udp` sends one OSC message to its own host/port: mapping `osc` address
and exactly one big-endian signed int32 argument, drawn from mapping `midi.value`
(current configuration range 0..127). OSC strings are NUL terminated and padded
to 4 bytes; type tag is `,i`. Address must start `/`, be 1..64 bytes, and contain
only ASCII letters/digits, `/`, `_` or `-`. There are no bundles or float/string
arguments in protocol 1.

For `/next`, value 100, the exact 16-byte datagram is:

```
2f 6e 65 78 74 00 00 00 2c 69 00 00 00 00 00 64
```

## Raw UDP / TCP

Output `udp` sends the mapping `payload` as one datagram, without a terminator.
Output `tcp` opens a connection for one cue, writes UTF-8 payload plus exactly one
LF byte, and closes it. The receiver must frame by LF and accumulate partial TCP
reads. TCP payloads containing CR or LF are rejected to prevent ambiguous frames.
Payloads are 1..128 UTF-8 bytes for sending; empty messages or TCP line breaks
can be stored but their raw-output actions are rejected at execution. There is no request/reply protocol,
automatic reconnect resend, credential, TLS, or destination authentication on
these output routes. Use a trusted local network.

For payload `next`, UDP bytes are `6e 65 78 74`; TCP bytes are
`6e 65 78 74 0a`. DNS resolves every enabled destination in the active profile in a separate background task; cues are rejected while
the hostname is unresolved. Numeric IPv4 avoids DNS. Socket connect runs in a
separate worker, with one active cue and a 750 ms pre-submission expiry. A cue is rejected if Wi-Fi is
unavailable, setup has opened, settings changed, or its deadline elapsed before
the actual send. TCP connection attempts use a 300 ms select deadline and exactly one nonblocking
write; partial writes are never completed by an automatic second write. A successful
socket send is shown as Sent, not Delivered. A partial/uncertain write is
Unknown and must not be retried automatically.

Example receiver on a computer reachable from the M5 (substitute that computer's
LAN address in the profile, port 8000):

```python
# UDP / OSC: print the exact received datagram.
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('0.0.0.0', 8000))
while True:
    data, peer = s.recvfrom(512)
    print(peer, data, data.hex())
```

```python
# Raw TCP: accumulate bytes until LF; accept a fresh connection per cue.
import socket
with socket.socket() as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', 8000)); s.listen()
    while True:
        c, peer = s.accept()
        with c:
            data = b''
            while b'\n' not in data and len(data) <= 128:
                part = c.recv(129 - len(data))
                if not part: break
                data += part
            print(peer, data)
```

`python3 firmware/tests/run_host.py` runs actual loopback listeners against the
same production codecs and socket transport for all three network formats,
including cancellation after connect and a refused connection. These tests establish
encoding and framing, not ESP32 Wi-Fi/RF performance or destination application
behavior. Physical tests must also cover disconnect during a note/key, MIDI
routing, failed Wi-Fi join, TCP reset/partial write and rapid input during display
refresh. Configuration/recovery details are in the shared configuration protocol.
