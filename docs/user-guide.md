# Using UberClicker

## Configure a device

Each TX and RX stores its own profiles, output destinations, mappings and Wi-Fi settings. Connect directly to the device you want to edit.

Connect a USB data cable and choose **Connect via USB serial** in the desktop browser app. Serial is trusted automatically and needs no screen code or prior physical setup. Connecting USB or reading settings leaves the BLE keyboard connected and cues running. Only an active settings edit pauses cues; Apply or Cancel resumes them. USB serial setup works while a Bluetooth keyboard/MIDI host is connected. If USB serial is unavailable in your browser, use desktop Chrome or Edge.

**Bluetooth configuration is disabled on both TX and RX.** To pair a computer for keyboard/MIDI output, press and release both buttons, select **Bluetooth pairing**, choose the device name shown on screen (for example **UberClicker RX: John Board**) in the host OS, and enter the displayed passkey. Exit pairing before testing clicks. Opening serial configuration does not enable Bluetooth pairing.

Choose **Edit settings**, make changes, and **Apply changes**. Cancel discards changes. If Apply times out, reread settings before retrying. An edit expires after two minutes without applicable activity. Both buttons open the menu; B1 moves and B2 selects.

A new or factory-reset device uses the last four uppercase hex digits of its Bluetooth MAC as its default device name, such as **0A8F**. Saved device names are preserved. BLE advertises **UberClicker TX: John Board** or **UberClicker RX: John Board**, using the device name; the default therefore advertises **UberClicker TX: 0A8F** or **UberClicker RX: 0A8F**. Applying a device rename updates advertising without disconnecting the output host. Legacy scan responses allow 29 UTF-8 bytes of name, so longer names are shortened without splitting a character.

## TX screen

The status bar shows the device name in place of the device/output type label, beside the mode icons and battery percentage. TX home also centres the device name in the body. Names wrap at spaces; the text scales to fill the available width and height. There are no button instructions on TX home. LoRa uses cellular bars (always full on TX; based on the selected TX’s RSSI on RX). Bluetooth, Wi-Fi, keyboard, MIDI, OSC waveform, UDP packet and TCP link icons identify active modes. A slashed Wi-Fi icon means disconnected. Battery percentage is an estimate from the existing voltage reading; an unavailable reading shows “--%”.

## Select a transmitter on RX

There is no radio pairing wizard and no device ID to copy. TX broadcasts its device name and device ID approximately every 10–12 seconds. RX shows up to eight recently heard TXs, with entries expiring after 45 seconds. Both devices must use the same radio preset.

RX home keeps the device name, signal/mode icons and battery percentage in the status bar above “Waiting for” / “Transmitters...” until a TX is heard. With one recent TX, a bold “Receiving:” heading appears above its device name, scaled to fill the available space, with “Last Seen: 1s” beneath it, showing time since its latest radio packet, including discovery broadcasts. Multiple recent TXs appear in a list, each with its own Last Seen line. The selected TX is highlighted in the list; five entries fit per page, and the page follows the selection. Device IDs, the “Listen to” heading and button hints are hidden on this screen.

With no saved selection, RX automatically selects and remembers the first TX it hears, so a single-TX setup needs no selection button press. On RX home, press **B1** for the previous TX or **B2** for the next TX. Each single press immediately selects it. RX remembers automatic and manual selections across power cycles. If the selected TX disappears, RX waits for it; it never silently switches to another TX. The first packet after discovery or a TX restart establishes a baseline and does not emit a cue.

TX sends Previous/Next broadcasts. RX never transmits radio acknowledgements, probes, or configuration data. TX therefore cannot report whether any RX received a cue. Check the destination application during rehearsal.

Radio broadcasts are open local discovery, without cryptographic sender authentication. Sequence numbers suppress ordinary duplicates; they do not prevent an attacker from impersonating a transmitter. This radio format is incompatible with the older paired-radio firmware: update both TX and RX together.

## Outputs

In **Edit settings**, each profile has **Previous button click** and **Next button click** lists. Use **Add action** to choose LoRa TX, BLE keyboard, BLE MIDI, OSC, UDP, TCP or Cloud TX. Each click can run up to eight enabled actions in order. Duplicate an OSC action and change its host/port to send the same click to another destination. Actions can be reordered, disabled or removed. For Cloud TX, first create a room in UberClicker Companion and save its TX pairing code under **Cloud connection** in the USB setup app. See [Cloud setup](cloud.md).

Add **LoRa TX** to broadcast that event; remove it for local-only clicks. RX runs its own actions when it hears the event from the selected TX. Wi-Fi settings and destinations belong to each device independently. Saved single-output settings migrate automatically, retaining inactive local mappings as disabled actions on LoRa-only transmitters. Update the app and firmware together.

In **Edit settings → Wi-Fi networks**, use **Add Wi-Fi network** for each network,
enter its name and password, then apply. Remove any entries you no longer need.
Choose **Keep** to preserve a saved password, **Replace** to change it, or **Clear**
for an open network. Renaming a network requires Replace or Clear. Passwords are
never displayed. The device tries the list in order, allowing 15 seconds per
network, and keeps trying until one connects. Removing all entries disables Wi-Fi
joining. List size depends on the device's available settings storage. Update the
app and firmware together; existing saved Wi-Fi credentials are preserved.

OSC, raw UDP and TCP use the device's Wi-Fi connection. The existing MIDI output is BLE MIDI. USB serial is for configuration/programming, not USB HID or MIDI.

## Standalone outputs and working examples

On TX, combine a **LoRa TX** action with local actions as needed. On RX, configure
actions for events from the selected transmitter. Add actions to each event in the active profile. Firmware
supports BLE keyboard, BLE MIDI, OSC/UDP, raw UDP and raw TCP. Native USB HID/MIDI
and RTP-MIDI are unavailable. Output settings and exact encodings are documented
in [outputs](outputs.md).

| Output | Configure and test |
| --- | --- |
| BLE keyboard | `ble_keyboard`, Previous usage 80 / Next 79, modifiers 0. Open **Bluetooth pairing** to pair the host OS with the displayed passkey, then exit setup. In a focused slideshow or keyboard event viewer, press/release B2 once for Right Arrow and B1 for Left Arrow. Firmware sends a zero report after each press. |
| BLE MIDI | `ble_midi`; Next type `note`, channel 1, number 60, value 100. Open **Bluetooth pairing**, pair, connect in the host's BLE MIDI utility, route to a MIDI monitor, then exit setup. Expect Note On `90 3c 64` followed by Note Off `80 3c 00`; BLE timestamp bytes precede the MIDI bytes. Host routing/release remains a physical test. |
| OSC over UDP | `osc_udp`, host = the computer's LAN IPv4, port 8000, Next OSC `/next`, MIDI value 100. Run the UDP listener below. Expect `2f6e6578740000002c69000000000064`, one OSC int32 argument. |
| Raw UDP | `udp`, same host/port, Next payload `next`. The UDP listener prints bytes `6e657874`; no terminator is added. |
| Raw TCP | `tcp`, same host/port, Next payload `next`. Run the TCP listener below. Expect `6e6578740a`, payload plus LF, and one connection per cue. |

For network outputs, provision Wi-Fi on the same trusted LAN as your computer,
allow the chosen local port through its firewall, and substitute the computer's
address for each action’s destination host. `127.0.0.1` on the M5 means the M5, not your computer.
These output routes have no destination authentication/TLS. Configuration secrets
are provisioned over trusted USB serial. Sending OSC/UDP/TCP on a LAN does not
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


## Reset and recovery

A refresh or disconnect never requires a factory reset. Reconnect over USB serial and reread settings.

For deliberate erasure: Both → Factory reset → B1 selects Erase → B2 arms → hold both for three seconds. This erases settings, selected TX and Bluetooth bonds. Firmware remains. Releasing early cancels.

This implementation has host tests and builds; physical radio, serial, Bluetooth and output acceptance still require testing on devices.
