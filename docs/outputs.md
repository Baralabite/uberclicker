# Standalone outputs

Implemented in the experimental M5 firmware, compile/host checked. BLE HID/MIDI
and Wi-Fi device behavior require physical acceptance before stable release.
Configure with the shared app contract: role `tx`, route `direct`, then select a
profile output. Receiver role uses its selected profile for received LoRa cues;
its local buttons never submit output.

All routes stop during physical setup, configuration editing and device menus.
No output cue is retried or replayed after reconnect. Busy inputs are rejected
without a queue. Profiles contain Previous and Next mappings, host and port;
there are at most 4 profiles. Profile names are 1..24 UTF-8 bytes, hostname/IPv4
at most 63 ASCII letters/digits/dots/hyphens, and port 1..65535. IPv6 literals and
URL schemes are not accepted. The config service validates complete settings;
codecs enforce wire payload bounds again at submission.

## BLE keyboard

Output `ble_keyboard` sends an 8-byte standard keyboard report containing modifier
bits and one HID usage, then a zero report after at least 30 ms. Keys 0..101 and
modifiers 0..255 match the configuration schema. Default Previous/Next are Left
Arrow (80) and Right Arrow (79). This is BLE HID, not USB HID through CH340.

Open physical setup, pair `UberClicker` in the host OS, and enter the six-digit
passkey displayed on the device. Exit setup before testing clicks. One BLE connection is supported at a time;
disconnect the output host before using the configuration app. A connected,
MITM-authenticated, encrypted, subscribed host and successful initial release are required. After
disconnect, the next connection receives zero reports before it can accept a
new press. Failed release remains pending and blocks another key. Success means
the BLE stack accepted the notification, not that PowerPoint or another app acted.
An old Just Works bond must be forgotten/re-paired through physical setup.

## BLE MIDI

Output `ble_midi` exposes MIDI service
`03b80e5a-ede8-4b33-a751-6ce34ec4c700` and characteristic
`7772e5db-3868-4112-a1a9-f2669d106bf3` on the same BLE server. Pair in physical
setup, then connect using the host's BLE MIDI tool and subscribe to notifications.
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

Output `osc_udp` sends one OSC message to profile host/port: mapping `osc` address
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
Payloads are 1..128 UTF-8 bytes for output; an empty payload may be stored in a
profile but its raw-output cue is rejected. There is no request/reply protocol,
automatic reconnect resend, credential, TLS, or destination authentication on
these output routes. Use a trusted local network.

For payload `next`, UDP bytes are `6e 65 78 74`; TCP bytes are
`6e 65 78 74 0a`. DNS resolves profiles in a separate background task; cues are rejected while
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
