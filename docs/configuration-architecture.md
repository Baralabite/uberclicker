# Configuration architecture proposal

Original architecture proposal, retained as a target. The [current implementation](../README.md)
now supplies the BLE configuration service and native/browser clients. The authoritative
contract is [config-protocol.md](../contracts/config-protocol.md). Protected LAN,
mDNS, hotspot, account backend and relay remain unimplemented; the broader access
paths below are not availability claims. Cues now remain paused for the full physical
setup window, including after Apply, Cancel or disconnect.

## One settings model

Use one versioned settings model and authenticated configuration service over BLE GATT and LAN. Native apps, browser clients, and the device menu share validation and migration rules. Include device role, button mappings, output mode/endpoints, radio profile and pairing, and Wi-Fi configuration; exclude GPS.

Reading settings must not expose stored secrets. Writes validate the complete staged configuration before an explicit Apply; Cancel discards edits. Reject stale revisions to prevent one client overwriting another client's changes. Pause keypress/click output while actively editing configuration, visibly indicate that state, and resume on Apply, Cancel, or a defined session timeout. Network/radio changes need a reconnect or recovery path so a lost connection cannot strand the user without local access.

## Access paths

| Client | Initial setup | Later configuration | Limits |
| --- | --- | --- | --- |
| Native iOS/Android app | Nearby BLE with physical claim, then provision Wi-Fi | BLE nearby or same-LAN discovery via mDNS, with manual IP fallback | App implementation, platform permissions, and discovery tests pending |
| Hosted browser app | Web Bluetooth in a supported browser, HTTPS, explicit user action | BLE; LAN only through a browser-compatible secure connection | Standard iPhone Safari/Chrome are not a supported Web Bluetooth path |
| Device-hosted web page | Explicit local Wi-Fi setup/fallback mode | Direct local browser access | Protect configuration with local authentication and explicit physical setup authorization |
| Device menu | Physical buttons | Core settings and factory reset | Small screen; use the same staged settings rules |

[Chrome's Web Bluetooth documentation](https://developer.chrome.com/docs/capabilities/bluetooth) describes supported platforms, secure-context requirements, and user-gesture device selection. The [implementation status](https://github.com/whatwg/bluetooth/blob/main/implementation-status.md) records browser gaps. Detect capabilities and provide a clear fallback; do not promise universal browser BLE support. A hosted HTTPS page reaching a local HTTP device is also not universally available: browser network/security restrictions mean direct device-hosted access must remain a separate fallback.

BLE configuration authorization and BLE keyboard pairing are separate concerns even if they share a radio. Define service access, bonding, session expiry, and physical authorization so a nearby stranger cannot configure the clicker. Apply the same authorization checks to LAN operations. Shared radio channels must not count as proof of ownership.

## Optional accounts and remote access

Default to local use without an account. An account may list claimed devices and saved profiles, but inventory does not make a device remotely reachable. Show online state only from an actual recent authenticated session, with an explicit stale/offline state.

Remote configuration is a later service: the device would initiate an authenticated outbound TLS relay connection. It requires physical device claim, ownership checks on every operation, revocation, scoped credentials, reconnect/expiry behavior, and an audit of account transfer/reset behavior. Do not expose inbound router ports or present relay configuration as implemented.

## Factory reset

1. Open Device → Factory reset. Explain the data loss and default focus to **Cancel**.
2. After selecting reset, require both buttons held for three seconds. Suppress ordinary click actions during confirmation. Releasing early cancels the hold; leaving the confirmation view cancels the operation.
3. Wipe Wi-Fi credentials, profiles, BLE bonds, radio keys, local authentication, and cloud tokens. Rotate access/claim credentials, preserve installed firmware, and return to unclaimed setup state.

An offline local reset cannot delete a server's old inventory record. The server must treat that record as stale until separately removed, and the device must reject former owner credentials after reset. Old inventory must never silently reclaim a reset device. Reclaiming or transferring it requires new physical authorization. A restart/reset button alone must not silently perform factory reset.

Before release, test interrupted reset and power loss, stale BLE bonds, old LAN credentials, revoked cloud credentials, failed Wi-Fi provisioning, conflicting clients, and recovery without an account.
