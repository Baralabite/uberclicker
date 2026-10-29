# UberClicker configuration protocol 2

Development contract; unreleased. The 120-second progress lease and durable Apply
receipt policy implement the focused review corrections; hardware acceptance is
still required.

Schema version 2, logical protocol 2; firmware version/revision remain independent.
BLE UUIDs and fragment header version 1 are unchanged from the first development
implementation. Protocol-1 apps must reject v2 status instead of overwriting it.
Normative model: `config.schema.json`; integration: `firmware/include/config_service.h`.

## Topology and authority

One Tx stores up to **eight** paired receivers. Each Rx stores exactly zero or one
controlling Tx. `peers[]` entries are `{deviceId,pairId,name,adminAuthorized}`; IDs/pair IDs must be
unique, device ID is 12 lowercase hex, pair ID 16 hex. Every relationship has its
own random 32-byte key. Pairing another Rx appends a relationship; it never replaces
an existing Rx or reuses a group key. `activePeer` selects the cue receiver and is
independent of the receiver selected for remote configuration. No implicit cue
broadcast or all-receiver Apply is defined.

Physically authorize BLE on both devices for initial pairing. Thereafter connect
the app to the **Tx**, choose any paired Rx, and read/edit/Apply/Cancel that Rx over
LoRa without reopening setup or touching buttons on it. Remote editing is permitted
only after core authenticates/decrypts the packet, checks controller/target/session
and replay protection, and calls `processRemoteRequest(controllerId,json)`.
The receiver independently checks the controller and `adminAuthorized:true`.
New physical pairing explicitly grants administration on both sides. Migrated v1
cue relationships retain their keys but set adminAuthorized:false: an authenticated
physical BLE session on each device must explicitly approve administration.
Upgrading firmware alone never grants an old cue peer configuration authority.
Never infer authority from radio channel, BLE keyboard bond, device ID or cached data.
Physical Rx reset/recovery remains available and clears controller keys/authority.

Remote edits may change profiles, active profile, output mappings/endpoints and
Wi-Fi (including write-only password). They may not change Rx role, controller
membership/key, activePeer or radio preset; these would strand the management path.
Such changes return `REMOTE_DENIED`. A fresh physical recovery/pairing flow handles
controller replacement. Removing an Rx on Tx removes its key there; remove/reset
or re-pair the former Rx before it can accept another controller.

## BLE authorization and framing

Core owns one Arduino BLE/NimBLE server lifecycle shared with HID/MIDI. Service
`8f4e1000-6b8d-4b37-9f4a-75b61e350001`; write-with-response
`8f4e1001-6b8d-4b37-9f4a-75b61e350001`; notify response
`8f4e1002-6b8d-4b37-9f4a-75b61e350001`. Subscribe before sending.
Physical setup lasts 120 s. Core displays the generated six-digit passkey and uses
SC MITM DisplayOnly. Both encrypted **and authenticated** must be true. Just Works
HID bonds do not authorize configuration. `claim` with displayed code returns one
random 32-byte owner token over that link; `authorize` uses it on later connections
and still needs physical setup on the device the app connects to (normally Tx).
Five failed attempts close setup. `revoke` clears owner/bonds; reset clears all keys.
Tokens use native Keychain/Keystore or browser memory only; settings never return
owner/radio/Wi-Fi secrets. Notifications target only the requesting connection.

UTF-8 JSON ≤8192 bytes. Every BLE fragment: `55 43 01 stream`, LE16 offset,
LE16 total length, then payload. At most 512 bytes per fragment; use negotiated
ATT MTU minus 3, safe default 20 (12 payload bytes). Strict sequential offsets,
fixed stream/total, no empty/duplicate/interleaved fragments; malformed UTF-8/JSON,
oversize or gaps discard partial state. Fragment idle limit 10 s, upload hard
limit 60 s, response send limit 10 s. Local client exchange limit 60 s. Remote
exchange limit 3600 s accounts for authenticated LoRa airtime; core must use bounded
fragmentation, retries for management only, adaptive deadlines and target isolation.
Cues remain non-retrying and must never be emitted by a configuration packet.

## Operations and bridge

Requests `{v:2,id:positiveInt,op:..., ...}`. Responses
`{v:2,id,ok:true,result:{...}}` or `{v:2,id,ok:false,error:{code,message}}`.
IDs ≤2147483647, error text ≤96 bytes, no echo of malformed/secret values.
`status` is public after BLE link security and includes identity/versions/capabilities.
`claim {code}`, `authorize {token}`, `read`, `begin`, `validate {session,config,secrets}`,
`apply {session,expectedRevision,transactionId}`, `cancel {session}`, `revoke`
preserve staged editing. Remote Apply requires a fresh 32-hex transactionId;
`result {transactionId}` performs authenticated durable outcome lookup.
Begin returns session/config/expiresIn; complete validation stages atomically.
Apply rejects stale revision, commits complete config/secrets once, increments
revision, and ends edit. Cancel discards it. Local sessions expire after 120 s. Remote Rx edit sessions expire after 120 s
without authenticated new accepted progress; replay/duplicate-only traffic does
not renew a lease. A full transfer has a separate 3600 s absolute ceiling and a
shorter size/airtime-derived transport deadline. A separate edit owner
prevents local and remote sessions from modifying each other's staging.

Local-only `updates` (no arguments) requires encrypted authenticated BLE, owner
authorization, physical setup and no local/remote edit or transfer. Capability
`ota_session:true` advertises an installed core hook. Success `{requested:true}`
queues opening the local clicker's update screen; it closes setup without checking
a feed or writing firmware. The clicker's physical Check and Install buttons remain
required. It is never permitted through the receiver proxy or remote radio API.

Tx proxy operation:

```json
{"v":2,"id":42,"op":"receiver","deviceId":"112233445566","request":{"op":"read"}}
```

Nested request has no `id`/`v`. Tx assigns a monotonic opaque bridge correlation
to forwarded JSON.id, distinct from BLE IDs that restart after reconnect. It
restores the original BLE ID only in the outer reply. Late callbacks must match
the bridge ID, so an old RxA reply can never be wrapped for a new RxB request. Allowed nested ops:
status/read/begin/validate/apply/cancel/result. Tx checks live BLE owner auth, setup lease,
Tx role and paired membership; capability `remote_config` is false until core calls
`setRemoteTransportAvailable(true)`. One remote request globally. Existing local
Tx edits return BUSY. Tx setup/management uses a 120 s idle lease. Queuing a new
authorized request starts it; ONLY authenticated newly accepted progress may renew
it, not rejected or duplicate traffic. `noteRemoteProgress(peerId)` is the core-to-
service hook. Core calls it only for a new accepted fragment/ACK advancing the
transfer (radio_admin.receive return 2), never replay/duplicate return 1. Thus long
valid uploads can finish while abandoned editing resumes within 120 s.

`takeRemoteRequest(receiverId,requestJson,correlation)` transfers the queued request
to core once; `remoteRequestActive(correlation)` lets core stop work after client disconnect
or a superseding bridge request (default 0 means any current transaction).
`completeRemoteRequest(correlation,responseJson)` accepts only matching v2 IDs and
returns `{deviceId,response:{v:2,id,ok,result|error}}` inside the outer result.
Failures must never become invented success. Core returns a normal correlated
`REMOTE_UNAVAILABLE`/`REMOTE_TIMEOUT` error for offline/bounded failure. Calls occur
on the core task, not BLE callbacks. Rx `processRemoteRequest` returns ordinary
v2 response JSON synchronously after encrypted transport reassembly.

Disconnect/Tx setup exit schedules a best-effort cancellation exposed by
`takeRemoteCancellation(receiverId,session)`. Core sends nested Cancel if the
management transport is available. Disconnect stops work and restores Tx to its
normal 120 s setup timeout. Even a lost Begin reply or Cancel cannot hold Rx output
beyond120 s after last valid new progress. Rx expiry is the cleanup backstop.
The app discards its draft on disconnect. No offline configuration queue exists.

## Secrets, persistence and uncertain outcomes

Secrets are `{wifi:{op:...},pairs:[{deviceId,op:...,value?:...}]}`. Wi-Fi ops are
explicit keep/clear/replace (8..63 UTF-8 bytes on replace); pair replace is exactly
64 lowercase hex. Every configured peer needs a key. Removed peer keys must be
explicitly cleared; unknown/duplicate operations and orphan keys fail validation.
Remote pair secret operations permit keep only. Empty SSID requires no password;
`wifi.configured` must match secret presence. Names/SSID/payloads are byte bounded.
Profiles ≤4; active indexes must exist; network outputs need host/port. OSC has one
int32 argument; raw TCP appends LF. Native USB is unavailable until verified.

NVS stores compact JSON objects as bounded blobs in alternating slots, with SHA-256
integrity. Legacy string/payload envelopes remain readable. Only the inactive slot
is reclaimed before a write, retaining the current complete record during GC and
power loss; successful readback selects the new slot. This avoids NVS's 4,000-byte
string limit without double-escaping a full configuration. Maximum four-profile,
eight-peer records are exercised with a 2 KB modeled Wi-Fi/bond/journal reserve.
Uncertain readback locks output/configuration until recovery; reboot can resolve
the atomically saved Apply receipt rather than replaying Apply.
Readback must verify a write before publishing RAM state. Torn writes retain prior
valid revision. Schema-1 single-peer records migrate in RAM to peers[0]/activePeer0
with the same cue key, owner, Wi-Fi and revision but adminAuthorized:false; the next Apply writes v2 atomically,
so power interruption never erases the old complete record. No migration guesses
new keys, shares keys across receivers, or silently erases unknown schemas.
Unsupported persisted schemas (or no valid record despite stored data) set
recoveryRequired, lock all configuration/claim/authorization and pause output.
Status explains recovery; only physical factory reset clears it. No unsupported
record is exposed as unclaimed default settings or overwritten by claim.
Development NVS does not claim confidentiality against physical flash extraction.

Radio management MUST use authenticated encryption for Wi-Fi passwords and pairing
material: cue HMAC alone is insufficient. Core's radio contract specifies exact
AEAD bytes/nonces/replay checks. Every fragment belongs to one receiver/controller,
fresh authenticated session and request; mismatched/interleaved/stale fragments fail.

Every remote Apply carries a random 32-hex transactionId. In the **same atomic NVS
record as the new configuration**, Rx stores a bounded last-Apply receipt containing
controllerId, pairing identity, transactionId, canonical internal request SHA-256
digest (session/expectedRevision/transactionId), and resulting revision. The digest
is never returned through ordinary reads or lookup. A repeated exact Apply from
that authenticated controller/pair returns the saved revision without another
commit, even after reboot/session expiry. Conflicting reuse returns
TRANSACTION_CONFLICT. Physical reset clears both receipt and authority.

`result {transactionId}` returns `{state:"applied",transactionId,revision}` only for
a matching retained receipt under current controller/pair authority. Otherwise it
returns `{state:"unknown",transactionId}`; absence/eviction/reset is never interpreted
as proof of non-commit or success. The app stores a pending ID before sending Apply
in native secure storage (browser memory only), blocks a new edit until result
resolution or explicit local discard, then reads current settings. A lost reply +
Rx reboot can therefore confirm exactly one commit without reapplying. Interrupted
persistence cannot publish a receipt independently of its configuration.

Never replay an uncertain Apply automatically. Unknown remains visible if lookup
cannot confirm it; discarding a lookup never undoes a committed change. Apply-all
is absent: each Rx owns its revision/result, and one success implies nothing about
another receiver.

Cues pause on the edited Rx for its edit session and on Tx while physically in
setup/management. Apply/Cancel ends the Rx edit; Tx stays paused until leaving setup
or expiry. Failed Wi-Fi joins retain LoRa controller access and physical recovery.

Errors: INVALID, UNAUTHORIZED, SETUP_REQUIRED, BUSY, STALE_REVISION, SESSION_EXPIRED,
TOO_LARGE, FRAMING, PERSIST_FAILED, UNSUPPORTED, RATE_LIMITED, REMOTE_DENIED,
REMOTE_UNAVAILABLE, REMOTE_TIMEOUT, TRANSACTION_CONFLICT, RECOVERY_REQUIRED.
Radio/Apply loss never proves success.
The required configuration path is native app → BLE → Tx → encrypted LoRa → Rx.
No backend, account, LAN endpoint or hosted web configuration is required. Optional
browser BLE uses the shared client where supported; any future LAN client must
validate HTTPS without HTTP secrets or certificate bypass.
