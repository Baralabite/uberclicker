# Firmware implementation

Experimental development build for the ThinkNode M5, 29 October 2026. It has
compiled successfully; no attached device has been flashed. Physical RF, BLE,
buttons, battery readings and e-paper timing remain acceptance work.

Build from the public checkout with `pio run -d firmware -e thinknode_m5`.
PlatformIO Core 6.1.19 is the tested runner. The source platform is pinned to
pioarduino `55.03.311`; its actual package reports Arduino **3.3.11**, not 3.3.1.
RadioLib 7.1.2, GxEPD2 1.6.9, Adafruit GFX 1.12.4, BusIO 1.17.4 and
ArduinoJson 7.4.2 are explicit pins. No private config header or compile-time
credential is required. A new device has no radio key and emits no radio cues
until paired through physically authorized configuration. Schema 2 supports eight Rx
relationships on a Tx and one controller on an Rx; every relationship has an
independent key, nonces, counters and replay window.

The board setting overrides the generic ESP32-S3 DevKit to **4 MB**, QIO/QSPI,
`partitions.csv` with two 0x1f0000-byte OTA app slots, no PSRAM. CH340 serial upload remains 115200; USB CDC on boot is
disabled. Stock native USB HID/MIDI is unavailable: CH340 programming evidence
does not establish ESP32 native USB data wiring. Packaging and offsets belong to
the release tools; the automatic factory image must not be used as an implied
settings-preserving update.

## OTA updates

OTA is disabled until an authorized local BLE app requests **Open update screen**
during physical setup. Active edits and radio work block that request. The core
opens only the local screen and closes setup; remote LoRa administration cannot
open it. Physical B1 checks the GitHub Pages release mirror, then a second B1 action installs
the displayed experimental version. Outputs pause throughout the session. The
worker uses IDF HTTPS with CA verification and a synchronized clock; it streams
SHA-256-checked firmware into the inactive slot, validates the application
identity, and only then switches the boot target. Matching settings/config/radio
versions and partition layout are required. A failed startup requests bootloader
rollback before confirming the pending app. See [installation and migration](flashing.md#opt-in-wi-fi-ota-updates)
for the one-time USB repartition, controls, publication and recovery limits.

`python3 firmware/tests/ota/run.py` runs the production worker with deterministic
HTTP/partition stubs and real OpenSSL SHA-256. It checks disabled-session gating,
physical install gating, compatibility, redirects, network failures, truncated and
corrupted images, image validation failure, inactive-slot writes, restart timing
and startup confirmation/rollback calls. It does not establish physical flash or
power-loss recovery.

## Hardware boundary

Initialization follows the read-only M5 reference, with GPS kept powered down.
No tracker, GPS parser/UART or upload endpoint is present.

| Function | Pins / initialization |
| --- | --- |
| Previous / Next | GPIO21 / GPIO14, active low, internal pullups |
| PCA9557 | Wire1 SDA48 SCL47, address 0x18; output register 1 = 0x10, direction register 3 = 0xc1 |
| SX1262 | CS17, DIO1=4, reset=6, busy=5; SPI SCK16 MISO7 MOSI15; power GPIO46 high |
| Radio frontend | DIO2 RF switch, 3.3 V TCXO, CRC2, boosted RX, current limit 140 mA |
| E-paper | Independent HSPI SCK38 MOSI45 CS39 DC40 reset=41 busy=42; UI worker owns this |
| Battery | GPIO8 divider, coarse voltage bands; no charging inference |
| GPS | GPIO11 low (off), reset13 high; no UART initialized |
| Buzzer | GPIO9 low; no audible acknowledgement implemented |

Radio preset `balanced` uses SF7; `long_range` uses SF12. Both currently use
917 MHz, BW125 kHz, CR4/5, private sync word, preamble 8, and 10 dBm. This is the
reference AU915 channel set's first channel. There is no universal regional
frequency capability: use requires the correct hardware/regional authorization.

## Integration boundary

`main.cpp` consumes the authoritative `ConfigService` and `device_ui::Controller`
headers. Configuration owns the JSON model, persistence and claim logic. UI owns
release/debounce/chord handling, reset confirmation, rendering and display task.
The core never copies the settings into a competing persisted format. The app selects the committed active profile; the minimal Tx menu exposes
receiver status, pairing and factory reset.

`outputs::begin` initializes one Arduino BLE stack/server. Keyboard HID, BLE MIDI,
and configuration attach to that same server. A second simultaneous connection
is rejected; disconnect the output host before connecting a configuration app.
Concurrent HID-host plus phone configuration is not implemented. Core forwards actual NimBLE `encrypted` and `authenticated` flags;
configuration never treats a Just Works bond as authorization. DisplayOnly Secure
Connections MITM uses the configuration-generated six-digit passkey. New pairing
requires physical setup; existing encrypted bonds can reconnect. Service discovery
must be tested on real hosts. The 29-byte advertisement carries config/HID UUIDs
and keyboard appearance; the 31-byte active scan response carries the MIDI UUID
and device name. Both payloads are checked against the 31-byte legacy limit.

Every output is paused during setup, staged editing and device menus. Entering a
pause invalidates pending network jobs and the radio session, and requests zero
keyboard/all-notes-off recovery. No cue survives reconnect. Buttons on a receiver
only navigate the UI. Display refresh and DNS/connect/write execute on separate
FreeRTOS tasks; the input/radio loop does not wait for them. Configuration Apply
can reinitialize the radio while cues are paused.

Reset uses the UI's three-second two-button confirmation. Core first verifies a
persistent `uc-core/reset` intent marker and restarts. Before advertising or
restoring station settings, the new boot completes the reset. ConfigService clears
its NVS records, ownership, profiles, Wi-Fi secret and pair key. Core deletes BLE
bonds and explicitly restores the Wi-Fi driver's persisted defaults, then clears
the intent marker and enters unclaimed setup. A failed step retains the marker,
locks output and reports incomplete reset over serial; restarting retries it.
This also prevents disconnect/bond-write races from reviving a bond after reset.
Physical power-interruption/reset acceptance is still pending.
Firmware is retained; no cloud inventory deletion is claimed. The development
build does not enable flash encryption or promise forensic secret erasure.

LAN configuration and setup hotspot are unavailable capabilities in this build;
BLE is the provisioning/recovery route. They are outside the current configuration scope; no backend or hosted
configuration service is required. See the configuration owner's protocol.

## Cue/session radio wire protocol 1

Exactly 80 bytes; all multibyte integer fields are big endian. No one-byte legacy
fallback. Authentication uses mbedTLS HMAC-SHA256, truncated to 128 bits, over
bytes 0..63. This authenticates commands and acknowledgements; payloads are not
encrypted. Keys are 32 random bytes generated during pair provisioning, scoped to
one Tx/Rx relationship; all-zero keys and identical/zero device identities are disabled.

| Bytes | Meaning |
| --- | --- |
| 0..1 | ASCII `UC` |
| 2 | Protocol version 1 |
| 3 | Kind:1 Hello,2 Challenge,3 Cue,4 ACK |
| 4..11 | Sender per-pair 64-bit pseudorandom identity |
| 12..19 | Destination per-pair identity |
| 20..35 | Fresh 128-bit transmitter nonce |
| 36..51 | Fresh 128-bit receiver challenge |
| 52..59 | Cue/ACK sequence, starting 1; Challenge carries receiver millis clock; Hello zero |
| 60 | Cue 1 Previous/2 Next; ACK 1 Received/2 Submitted/3 Rejected/4 Unknown/5 Sent |
| 61..63 | Cue/ACK estimated receiver clock, low 24 bits; handshake zero |
| 64..79 | First 16 bytes of HMAC-SHA256 |

Wire identities are the first eight bytes of HMAC-SHA256(pairKey,
`55 43 2d 52 41 44 49 4f 2d 49 44 00 || deviceIdBE64`). The domain string is
`UC-RADIO-ID` plus NUL. This derives independent per-pair identities without a
second persisted ID model; readable configuration still uses its MAC-derived ID.
The MAC identity itself is not broadcast in the radio packet.

The sender boots with a new random nonce. Hello authenticates both identities.
The receiver generates a fresh challenge for each new sender nonce or invalidated
session, then sends an authenticated Challenge bound to that nonce. The receiver can answer an identical Hello without resetting its counters; Tx
uses a new nonce for every negotiation attempt. The transmitter accepts
only its current nonce while negotiation is pending. Once established, unsolicited
or replayed Challenges cannot reset traffic keys or AEAD counters. A cue includes both nonces, a monotonically increasing
sequence, and the receiver-clock estimate. The receiver consumes a sequence
**before** submitting output. A duplicate only repeats the previous result; older
sequences, wrong peers/keys, stale challenges and forged ACKs are discarded.
Rebooting either endpoint breaks recorded sessions without relying on flash
sequence writes. Setup/menu pause rotates the sender nonce or invalidates the
receiver challenge. Idle sender sessions renew after 60 seconds; receiver sessions expire after
120 seconds without session/admin activity. Active bounded management transfers
retain their current session until the transaction ends. No cue retries occur.

Challenge clock transfer includes measured packet airtime. Receiver cue age must
be within -250 ms through airtime+500 ms, using 24-bit modular time subtraction;
this rejects delayed first deliveries as well as already-seen replay. Session
lifetime stays far below the modular half-range. Host tests cover this through
32-bit clock rollover. Real oscillator/scheduling tolerances remain RF acceptance.

RadioLib computes the 80-byte airtime using the active modem configuration.
For explicit header/CRC/preamble 8 at 125 kHz, approximate single-frame airtime is
144 ms at SF7 or 3285 ms at SF12. Core uses actual `getTimeOnAir` microseconds,
rounded up, and an outcome deadline of `3*airtime+1000` ms from cue send. Discovery
and command/ACK each require multiple frames; old one-byte latency claims do not
apply. A lost ACK yields Unknown and restarts discovery with a fresh session, never
an automatic cue retry. A Received ACK can
precede a later Submitted/Sent/Rejected/Unknown result for network output.

## Encrypted receiver administration: radio envelope 2

The app connects to a physically authorized Tx and selects a paired receiver.
`peers[]` and `activePeer` come from configuration schema 2. Administration selects
its own target by device ID; `activePeer` still selects exactly one cue destination.
There is no broadcast cue or Apply-all. The runtime table holds at most eight
independent sessions and keys. Only one management transaction uses the half-duplex
radio at a time; other requests receive bounded backpressure instead of an offline
queue. Every receiver must initially be paired physically with the Tx.
`peers[].adminAuthorized` is an explicit physical grant on both devices; migrated
schema-1 cue pairings default it to false and keep their cue key without gaining
remote configuration authority. Grant changes start a fresh radio session. All peers
must use the Tx's shared radio preset/frequency.

Core consumes the authoritative `takeRemoteRequest`, `completeRemoteRequest`,
`takeRemoteCancellation` and `processRemoteRequest` API. Plaintext settings exist
only inside the trusted device tasks after AEAD verification; they are never sent
as cleartext LoRa. The receiver service checks its paired controller independently
and permits remote profile/output/Wi-Fi changes, preserving radio settings and
controller authority. Local receiver reset clears this authority. Configuration
Apply updates the existing runtime settings without resetting unchanged radio
sessions/counters, so its response can still reach the controller. Wi-Fi join
failure cannot remove the LoRa or physical recovery path.

Management frames use AES-256-GCM from mbedTLS, full 16-byte tags. The existing
HMAC Hello/Challenge establishes fresh transmitter/receiver nonces first. No
configuration packet can enter the cue decoder. Management header version 2 and
kind 5 are distinct from cue/session header version 1 and from BLE framing header
version 1; logical configuration protocol/schema are version 2.

| Bytes | Management meaning |
| --- | --- |
| 0..1 | ASCII `UC` |
| 2..3 | Version 2, kind 5 |
| 4..11, 12..19 | Source and destination per-pair wire identities |
| 20..35, 36..51 | Current transmitter nonce and receiver challenge |
| 52..59 | Directional monotonically increasing AEAD packet counter, starting 1 |
| 60..63 | Nonzero transport transaction correlation, independent of JSON request ID |
| 64..65, 66..67 | Big-endian payload offset / complete JSON length |
| 68 | 0 request, 1 response, 2 request ACK, 3 response ACK |
| 69 | Ciphertext payload length, 1..160 for data or 0 for ACK |
| 70..71 | Reserved zero |
| 72..(72+n-1) | AES-GCM ciphertext, n ≤160 |
| (72+n)..(87+n) | 16-byte authentication tag |

Complete frames are 88..248 bytes, below the SX1262's 255-byte bound. All 72 header
bytes are authenticated associated data. All multibyte values are big endian.
Messages are nonempty UTF-8 JSON up to 8192 bytes. Header offsets/totals, identity,
direction, correlation, session, counter and GCM authentication must pass before
bytes enter reassembly. Counter reuse, stale packets, foreign peers, wrong keys,
wrong direction, gaps, changed totals and interleaving cannot execute settings.

The traffic key is HMAC-SHA256(pairKey,
`"UC-ADMIN-v2\0" || txNonce16 || rxChallenge16 || directionByte`), using direction
1 for controller→Rx and 2 for Rx→controller. The GCM IV is four zero bytes followed
by the BE64 directional counter. Independent directional keys and fresh session
nonces prevent IV/key reuse after either endpoint restarts. Admin counters are
not reset by a repeated Hello/Challenge for the same live session. A changed
session aborts an in-flight transaction; every new management request first
negotiates fresh nonces so result lookup also works after an Rx reboot; core never resends the complete operation
automatically into a new session.

Fragments use stop-and-wait. An encrypted ACK names the committed next offset.
A new authenticated response can implicitly acknowledge the last request fragment.
Each fragment gets at most three transmissions of the **same ciphertext/counter**;
receivers recognize exact duplicates and return the same receipt/result without
executing Apply twice. Responses remain cached for the live transaction. A new
request can acknowledge a fully received prior response if its final ACK was lost.
Only the selected peer's session can advance that transaction. Transport IDs are
unique during the controller boot; JSON request IDs retain their application
correlation and may be reused safely after a fresh radio session.

Timeouts derive from `getTimeOnAir(248)`: fragment timeout is `3*maxFrameMs+1000`,
allowing data, lost ACK and the first response to finish without a premature cue-
style retry collision. A fragment gets two retries, then the transaction fails.
Reassembly/response silence is bounded by three such timeouts; total transaction
lifetime is derived from request/response fragment counts and that airtime
budget, capped at 3600 seconds. Unknown response length initially uses the 8192-byte
bound; receiving its total refines the deadline. At SF12, a 248-byte frame is approximately
8856 ms and an 88-byte management ACK 3613 ms. An 8 KB upload/download can take
minutes; this is not an instant network substitute. The SF12 host timing model
covers loss of the final request ACK with both radios able to collide on air.

Cues pause on the edited Rx only; other receivers keep their independent state.
Tx's own inputs remain paused while it is in physical configuration setup. Remote
editing has a **120-second inactivity backstop** in core: only newly accepted
authenticated forward progress refreshes it, through
`ConfigService::noteRemoteProgress(peerId)`. Exact replays, duplicate bytes with
a new packet counter, unrelated correlations and repeated ACKs cannot renew it. Expiry cancels the staged edit
through the existing service API. This is separate from the size/airtime-based transfer deadline, capped at
3600 seconds. Both Tx setup/management and Rx edits use the 120-second inactivity
bound, as shown by the app. Disconnect/exit sends a best-effort authenticated Cancel; idle cleanup
also covers disconnect during Begin before the app learns the session token.
A continuously active transfer cannot extend the hard ceiling.

A lost/invalid Apply reply is `REMOTE_TIMEOUT` with **unknown outcome**. No whole
Apply is retried. The configuration service requires a random 32-hex transaction
ID for remote Apply and atomically persists **one most recent receipt per Rx** with the
new configuration. A later Apply evicts that prior receipt. `result {transactionId}` is an authenticated read-only lookup
scoped to the current controller/pair. The receipt records the request digest and
resulting revision; conflicting transaction reuse is rejected. A fresh encrypted
session can query it after Rx reboot without reapplying. Unknown/evicted receipts
remain unknown; rereading actual configuration does not prove which transaction
committed. Core carries that contract's JSON unchanged and confirms the UI only
for a correlated authenticated response with a matching transaction ID. A live
radio cache alone is not reboot-durable proof.

The core fills the UI's remote target/operation/pending/confirmed/rejected/unknown
and receipt-result fields separately from the cue receiver ID. The Rx uses actual
remote-edit state and the authenticated controller identity. Recovery-required
settings keep outputs blocked while retaining the physical reset menu. Starting
a fresh physical setup clears old remote presentation so the BLE passkey stays
visible. Core does not modify the UI renderer or configuration model.

Physical RF timing, maximum-length transfers, leases, reboot/result recovery,
multiple real receivers and mobile BLE remain acceptance work.

## All-receiver radio health

Tx probes every configured Rx independently of cue selection. Pairing alone never
sets Connected. Core captures RSSI and SNR immediately after `readData`, before
restarting receive, then binds them to a peer only after authentication and
freshness/replay/target checks. A new pending cue ACK, a newly accepted admin
fragment/ACK, a fresh handshake response, or a matching liveness response may
update health. Repeated ACKs, duplicate fragments, unsolicited/stale Challenges,
wrong peers, forged packets and uncorrelated responses cannot refresh it. Signal
on the Tx screen is **Rx→Tx RSSI measured at the Tx**, not an Rx-reported Tx→Rx
measurement or proof of output/application success. SNR is captured alongside it.

Liveness uses a separate 52-byte HMAC-SHA256 envelope, with no output/configuration
operation and no config-lease renewal:

| Bytes | Health meaning |
| --- | --- |
| 0..3 | `55 43 02 06` request / `55 43 02 07` response |
| 4..11, 12..19 | Source/destination per-pair wire IDs |
| 20..35 | Fresh random 128-bit Tx probe nonce, echoed by Rx |
| 36..51 | First 16 HMAC-SHA256 bytes over bytes 0..35 |

Each request gets one attempt; no cues are retried or broadcast. Only the first
valid response matching the outstanding fresh nonce and deadline is accepted.
A recorded response cannot answer the next probe. Receivers suppress identical
request replay and rate-limit replies. Liveness does not require an administrative
grant and never extends a configuration edit. It also does not change cue/admin
session nonces, keys or counters.

Let H be the actual `getTimeOnAir(52)` milliseconds. Probes rotate through all
paired receivers, spaced by `max(10000,20*H)` milliseconds globally. This limits
steady Tx probe airtime to at most 5%, often less; it is not a regulatory approval.
Response deadline is `2*H+1000`. With N paired receivers, freshness expires after
`N*spacing+2*H+2000`. An unanswered probe marks that receiver offline sooner unless
other newly authenticated traffic confirmed it meanwhile. Staleness is latched
until a new valid observation, so a clock wrap cannot resurrect old healthy bars.
Unknown boot state stays unknown until heard or probed; disabled monitoring shows
unknown rather than old healthy signal. The UI averages only fresh connected
receivers with valid RSSI, retaining a separate offline/unknown count and flag.

At SF7 H is approximately 103 ms: eight Rx are checked about every 80 s and their
freshness bound is about 82 s. At SF12 H is approximately 2466 ms: eight Rx take
about 6.6 minutes per round, with a freshness bound about 6.7 minutes. Initial
counts populate over that scan, not instantly. These are modem/host-model figures,
not bench measurements. Active management, cues, held buttons and firmware update
work take precedence or defer probes. A busy radio rejects an immediate cue instead
of queueing it; long-range background probes can occupy roughly five seconds for
a request/reply. A device can be radio-online while its local output is unavailable.
Physical RF/region compliance, timing, RSSI calibration and multi-device testing
remain release acceptance gates.

## Checks and remaining acceptance

`python3 firmware/tests/run_host.py` compiles the production protocol/codec/safety
logic with assertions, uses OpenSSL as the host HMAC/AES-GCM provider, and checks actual
local UDP/TCP listeners using the production socket transport. Firmware compiles the mbedTLS path. Coverage includes
forged commands/ACKs, wrong identities/keys, duplicate/stale sequences, sender and
receiver reboot, captured Hello/Challenge replay, pause/session renewal, delayed
cues/clock rollover, MIDI bounds/release bytes, reconnect release gates, stale
network generations, payload limits, exact OSC/UDP/TCP bytes, cancellation after connect with zero
bytes, refusal without retry, and reset intent persistence/failure through a
host NVS stub. `admin_test.cpp` adds encrypted eight-peer isolation, forged and
reflected management traffic, replay/reboot boundaries, complete 8 KB request and
response fragmentation, lost ACKs, live execution/result deduplication, new-session
correlation reuse, bounded failures, idle rollover and a half-duplex SF12 timing
model. `health_test.cpp` covers fresh nonce probes, forged/wrong-peer/replayed
responses, no AEAD counter reset from unsolicited Challenges, stale-state latching,
missing replies, invalid signals and clock rollover. The configuration owner's actual-service harness separately verifies remote
authority, atomic revisions, forbidden remote fields, migration and uncertain
outcomes; host models are not real radio/BLE verification.

UI-owned checks under `firmware/tests/ui/` cover button debounce, startup-held
inhibition, no held repeat, chord suppression/reset and rollover. Build success
and host checks do not establish real radio range/timing, panel responsiveness,
BLE notification delivery, MIDI host behavior, bond removal, battery calibration
or physical flash/recovery. Those gates must pass before stable promotion.
