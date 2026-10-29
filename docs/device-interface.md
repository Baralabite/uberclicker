# UberClicker device interface proposal

The recommended direction is **Quiet action-first**: a compact role header, coarse battery indicator, large profile name (“Presenter”) at home, and two short lines of context. The primary label becomes the last action or a condition needing attention. **Status-first** promotes acknowledgement before a local “Ready” label, which means local click handling is ready, not that a remote device is connected. **Split actions** keeps larger Previous / Next labels visible without repeating them in the footer. The preview shows one complete design at a time and keeps each design's selected scenario independently.

This is a reviewable interface proposal. It does not connect to a device, configure a real account, emit a key, or send a network command. Browser interactions are instantaneous; the existing e-paper full refresh takes approximately two seconds. The prototype deliberately has no progress animation or rapidly changing telemetry.

## Display and controls

- The source display is a 200 × 200 monochrome GxEPD2_154_D67 panel. Each preview canvas has exactly those logical dimensions and is enlarged only for review. Device artwork uses black and white in both host themes.
- The screen uses approximately 12 px horizontal margins, 24–28 px primary labels and 14–16 px context where possible. Compact menu/footer text is 11–13 px. Battery is a coarse bar, with no percentage or charging inference. Voltage thresholds need hardware calibration.
- Two abstract controls are called B1 and B2. Their location and enclosure are intentionally unspecified. The existing rotary power/brightness control remains physical; this proposal does not pretend e-paper has a software brightness slider. GPS contributes no home-screen information.
- On the presentation screen, release B1 for Previous and B2 for Next. A simultaneous two-button gesture cancels the pending individual click and opens Menu. No action repeats while a button is held. Firmware needs debounce, a short chord-detection window and an input state machine so releasing the first button in a chord cannot emit a stray click.
- In Menu, B1 moves and B2 selects. Both returns home. Modes are configured through the app; the preview's mode selector is a review control. The browser offers a separate Both button to simulate a physical chord without assuming button geometry. Keyboard activation also acts on release.
- Main Menu has Configure, Device and Back. Configure contains App setup, LAN setup, Wi-Fi fallback and Back. Device contains Pair receiver, Factory reset and Back. Four rows maximum preserve readable spacing. Setup screens visibly say “Clicks paused”. The primary screen has no slide number, click count, GPS, RSSI, diagnostic dashboard or heartbeat claim.
- In receiver mode, individual physical buttons refresh local status and emit no presentation/output command. Use the preview's Last action scenario to simulate an incoming click, separately from physical-button behavior.

## What the screen can truthfully say

| Scenario | Wording | Meaning |
| --- | --- | --- |
| LoRa transmitter, before an ACK | No acknowledgement yet | The existing protocol has no heartbeat; this is not a connection indicator. |
| LoRa transmitter, ACK received | Last click acknowledged | A receiver acknowledged receipt. It does not prove the presentation advanced or an output succeeded. |
| LoRa transmitter, timeout | Unconfirmed / Delivery is unknown | A packet may have arrived without its acknowledgement. Do not automatically resend a presentation command just because the ACK was lost. |
| LoRa receiver | Last received / Output unverified | Receipt and downstream output are separate facts. |
| Receiver without recent activity | Link unknown / No link heartbeat | Silence cannot establish that the transmitter is offline. |
| Direct OSC concept | Last command sent / Delivery unverified | Local send completion does not establish target application behavior. |
| USB keyboard concept | Key event sent / Host action unverified | A future HID report does not establish host application behavior. USB home remains explicitly a concept requiring hardware checks. |

LoRa transmitter and receiver are current source roles. Direct Wi-Fi OSC/UDP/TCP and MIDI are proposed outputs; MIDI transport is still undecided. The OSC example is UDP, not a guarantee that every proposed transport has been implemented. USB currently provides programming/serial access in the reference source, which does not establish USB HID or USB MIDI support.

The requested USB keyboard behavior should be available as a receiver output and as a standalone clicker output, subject to hardware validation. ESP32-S3 native USB uses GPIO19/20, which the current source assigns to GPS. Removing GPS from the UI does not establish that the board's USB-C connector exposes those native USB pins; it may expose only a USB-to-UART bridge. Verify the board schematic, connector path and firmware pin allocation before choosing implementation hardware. The preview labels USB as a proposal and does not report it as ready.

## Configuration and pairing proposals

The primary onboarding path is the native UberClicker app over BLE. The device setup screen says “Open UberClicker app” and shows a visible device identity and pairing code. The preview's **UberClicker A7** and **482 916** are demonstration values, not credentials or a working pairing flow. The native app can use the LAN when the device is already connected to Wi-Fi; the preview's `http://192.168.1.42` is an example device address.

A web app can offer BLE setup where browser support permits and account linking as an alternative. iOS Safari and Chrome do not provide native Web Bluetooth, so account linking or the native app must cover that route. Account linking also needs an explicit device authorization and ownership design; it is not implied by joining the same LAN.

The optional Wi-Fi fallback is a local device hotspot, illustrated as **UberClicker-Setup**, followed by `192.168.4.1`. Its credentials, authentication, captive portal behavior and lifecycle need implementation design. No QR is shown because there is no real setup payload to encode.

The reference LoRa protocol uses a matching one-byte channel, not authenticated pairing. The proposed Pair receiver screen sketches a same-code confirmation on both devices. Device identities, key provisioning, binding, replay resistance, unpairing and recovery remain protocol work; the screen does not claim the current channel check supplies them.

## Factory reset

Factory reset is a deliberate action under Device. It opens **Erase settings?** with **Cancel selected** by default. B1 changes between Cancel and Erase; B2 selects. Selecting Cancel leaves settings intact. Selecting Erase arms a second confirmation: hold both buttons continuously for approximately three seconds; either individual button cancels, and releasing early cancels the hold. Confirmation should clear local Wi-Fi credentials, BLE bonds, app pairing and profiles, then return to onboarding. The browser demonstrates this using its own local scenario state only. Holding both from the initial Cancel selection cannot bypass the choice.

Remote account unlink is separate, especially when the device is offline. The reset screen explicitly calls this out. A production app must explain any remaining server association and provide an account-side unlink/recovery action. Reset must not silently promise deletion from a remote account it cannot reach.

## Preview and verification

Open [device-preview.html](../device-preview.html) in a browser for the carousel and interactions. No build, package install or server is needed to view it. The editable design source is [designs/device-interface.html](../designs/device-interface.html); the standalone preview includes the runtime shell that provides the carousel and local state storage.

After editing the source, run this command from the repository root with Python 3:

```sh
python3 scripts/build-device-preview.py
```

The standard-library script updates only the embedded design inside the checked-in preview shell. Commit both the source and rebuilt preview. It requires no Codex installation, local skill files, or external build packages. The design works without host-only design controls; CDN resources used by the preview shell need internet access for supplementary tooling.

Each design supports mode and state selection, individual click simulation, menu navigation, app/BLE setup, LAN setup, fallback hotspot, pairing proposal, low battery and reset. Optional host design controls adjust the primary label from 24 to 28 logical pixels. Scenario choices are remembered by the conversation host when available; no secrets are stored.

The preview includes `document.getElementById('uberclicker-device-designs').ucDesignCheck()` in its iframe. It measures text across the three designs, four modes, screen states, each menu selection and the 24/28 px label extremes. It reports over-wide text instead of squeezing text to fit.

Browser verification on 29 October 2026 passed **456 text-fit combinations**, keyboard release/no-repeat, individual action and menu navigation, app setup navigation, unconfirmed results, receiver buttons without output, keyboard-specific USB simulation, carousel switching, reset default-choice protection, Cancel, short-hold cancellation and three-second confirmation into onboarding. Layout was checked at 320 and 736 px in light/dark appearance, with no horizontal overflow or script errors. Screenshots of all three designs and the narrow menu were visually inspected.

Remaining checks are physical e-paper rendering, exact firmware font metrics, voltage calibration, input/chord timing on hardware, USB wiring, and all future BLE/LAN/account/output protocols. No firmware, original source, account configuration or published site is changed by this proposal.
