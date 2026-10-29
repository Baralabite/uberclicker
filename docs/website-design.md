# UberClicker website design preview

Status: early project and design preview, 29 October 2026. Reviewed locally, published to GitHub Pages by request, and verified in production.

## Direction and scope

The site treats UberClicker as a dedicated stage-control instrument. The most memorable element is the device itself: an actual lit WebGL mesh next to an oversized condensed heading. The rest of the page stays quiet and explains how the proposed system fits together.

The compact design plan was checked against the brief before implementation. A generic dashboard, metric blocks, card grid and continuous product spin were rejected: none helps explain a two-button cue controller. The chosen cream/black/yellow palette was explicitly supplied by the design plan, not inferred as a default style.

- Paper `#f3f1e9`, ink `#161714`, muted text `#61635b`, signal yellow `#f2d542`, rule `#d1d1c6`, near-white `#fffefa`.
- Barlow Condensed Bold carries the object-scale headings; Barlow carries copy and controls. Fonts are local, with license files and `font-display: swap`.
- Desktop pairs a left-aligned headline with the device on the right. Mobile stacks the object below compact copy. Section progression is hero → signal route → device UI → configuration → release state.
- Rules separate actual subjects. Signal yellow marks a primary action and the single horizontal transition. No decorative metrics, testimonials, invented performance, price, download or flash action appears.

```text
[UberClicker]      system / interface / configuration      [design preview]
[Two buttons.             [lit ThinkNode M5 mesh]
 Your next cue.]          [model/source caveat]
[explanation + real anchor links]
[signal-yellow transition]
[Tx] -------- LoRa ------ [Rx] ---- proposed USB HID ---- [computer]
[planned standalone OSC / MIDI / UDP / TCP]
[device UI explanation]  [quiet Presenter screen + demo buttons]
[configure explanation]
[BLE / LAN / future linked-device app concept] [connection limits]
[factory reset disclosure]
[in-development release statement]
```

## Implementation

`website/dist/index.html`, `style.css`, `app.js` and `device.js` are buildless. Three.js 0.170.0 is vendored under its MIT license; no framework or package installation is needed. CSS, JavaScript, SVG and fonts are served locally with no runtime CDN calls.

The real procedural mesh approximates the M5’s 82 × 51.6 × 26.3 mm body, square 1.54-inch display, antenna, rotary power knob, two side buttons, unused GPS toggle, seams and indicator strip. Its proportions and parts follow Elecrow’s official manual and reference photograph. It is prominently labelled illustrative, not manufacturing CAD; antenna proportions, shell details, materials and lighting are approximate.

A fine pointer on desktop changes yaw/pitch by at most about 10 degrees around the resting pose and returns home on exit. The renderer schedules frames only while easing or resizing; there is no continuous rotation or permanent animation loop. Motion is disabled for reduced-motion preference and devices without a fine hover pointer. Narrow desktop browsers retain pointer interaction. The motion is decorative: no information requires rotation or dragging.

A locally authored SVG remains in place until WebGL succeeds. A module-load timeout and WebGL creation/context-loss handling show an informative static fallback. ResizeObserver preserves the camera aspect ratio; rendering resolution is limited to 2× device pixel ratio.

The sample screen uses a 200 × 200 logical texture, enlarged on the website. It shows LORA TX, coarse illustrative battery bars, Presenter, button meanings and historical acknowledgement. It never invents a slide/cue counter, numeric battery percentage, RSSI or live link indicator. The independent HTML demo is intentionally non-connected: pressing Previous/Next changes only a last-action label and an explicitly simulated acknowledgement.

The app preview uses native radio controls to show BLE, local network and future linked-device explanations. It does not discover devices, request browser Bluetooth permission, sign in, modify settings or call a backend. Its selection is local ephemeral preview state. The reset disclosure is explanatory and cannot reset anything.

## Product truthfulness

- The intended pair is Tx → LoRa → Rx → USB keyboard → computer, but native USB keyboard output on the M5 is unverified. The device’s USB-C serial bridge may require an external bridge or hardware modification for HID. The route uses a dashed proposed leg and a visible caveat.
- A standalone transmitter can have planned direct OSC, MIDI, UDP and TCP outputs. These are labelled planned; no working firmware feature is asserted by this preview.
- Native configuration over BLE is the first direction; LAN setup requires device Wi-Fi connectivity and network reachability. Local use does not require an account.
- Configuration uses a deliberate paused-output session with Apply/Cancel in the proposed real app. The website illustrates the connection choice only.
- Web Bluetooth is a supported-browser alternative, with no universal Safari or iPhone claim.
- Account linking alone does not create reachability. Remote access is a future authenticated cloud-relay architecture requiring an online device.
- A planned factory reset clears settings, pairings and local credentials while preserving firmware. Cloud unlinking is separate when online. GPS is unused.
- The page calls itself a design preview, and the release section states that firmware, apps and hardware validation remain ahead. There is no manufactured download or release availability.

## Skills and source material

Applied skills:

- [Frontend design](https://github.com/anthropics/skills/blob/main/skills/frontend-design/SKILL.md) — subject-specific visual plan, type/palette/layout, object-led composition, two-pass critique and screenshots.
- Web Interface Guidelines — fresh online rules fetched 29 October 2026 from [Vercel’s source](https://raw.githubusercontent.com/vercel-labs/web-interface-guidelines/main/command.md); checked semantics, focus, labels, motion, contrast, image sizing, overflow and failure states. Sentence-case labels follow the frontend skill and the project’s direction.
- Sites — presentation-led progression, local preparation and preview, followed by GitHub Pages publication by request.
- Ponytail — static platform-first implementation, one render dependency, no backend or account implementation, and a small standard-library check.

Product and technical references:

- [Elecrow ThinkNode M5 wiki](https://www.elecrow.com/wiki/ThinkNode_M5_Meshtastic_LoRa_Signal_Transceiver_ESP32-S3.html).
- [Elecrow official user manual](https://www.elecrow.com/download/product/ILM13205D/ThinkNode_M5_User_Manual.pdf), device-parts drawing and dimensions.
- [Elecrow official appearance reference](https://www.elecrow.com/wiki/assets/images/ThinkNode_M5_Meshtastic_LoRa_Signal_Transceiver_ESP32-S3/feature_of_Thinknode_M5_1.webp), examined for black shell, antenna, knob, square screen and side buttons. Photo not redistributed.
- [Espressif ESP32-S3 USB Device Stack](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-reference/peripherals/usb_device.html), reference for native USB pin constraints. Product-level USB bridging remains a hardware-validation issue.
- [Three.js 0.170.0 distribution](https://cdn.jsdelivr.net/npm/three@0.170.0/build/three.module.min.js), [MIT license](https://cdn.jsdelivr.net/npm/three@0.170.0/LICENSE).
- [Barlow font license](https://github.com/google/fonts/blob/main/ofl/barlow/OFL.txt) and [Barlow Condensed font license](https://github.com/google/fonts/blob/main/ofl/barlowcondensed/OFL.txt).

Manufacturer research files are excluded from the source tree; only authored geometry/illustration and licensed font/Three.js assets are included.

## Verification

Local server: `python3 -m http.server 4173 --bind 127.0.0.1 --directory website/dist`, started from the repository root. Preview: <http://127.0.0.1:4173>. Entry: `website/dist/index.html`.

- HTTP 200 for the representative page.
- Browser screenshots inspected at 1440 × 1000 and 390 × 844. Both document widths equal viewport widths; no horizontal overflow. Full mobile page inspected for route, interface, configuration and footer.
- Real Three.js canvas rendered successfully; pointer movement visibly changes the mesh orientation, and pointer exit returns toward its resting pose. Normal browser run produced no console warning/error.
- Reduced-motion emulation gives a fixed view. A narrow desktop viewport retains fine-pointer interaction; touch/coarse-pointer input gets a fixed view. Emulation was cleared after the check.
- Forced WebGL context loss revealed the informative SVG fallback and its accessible description. Page reloaded after the check to restore normal WebGL rendering.
- Previous/Next sample controls update the historical action and explicitly simulated acknowledgement. Native radio selection changes the explanation; arrow-key navigation works. Factory-reset disclosure opens correctly.
- Keyboard Tab reaches actual links with a visible focus outline. Semantic main, navigation, headings, skip link, native controls, labels and polite announcements are present.
- `node website/check.mjs` checks assets, anchors, unique ids, sample actions, connection choices and absence of invented status/counters. JavaScript syntax checked with Node.

Review captures live outside the served directory in `website/qa/`: `desktop-1440.jpg`, `mobile-390.jpg`, `interface-desktop.jpg`, `webgl-fallback.jpg`.

Known limits: this is not a hardware test, production accessibility audit or cross-browser device lab. No native app, actual BLE/LAN session, cloud relay, output protocol, USB HID or firmware behavior was implemented or validated. Approximate geometry is unsuitable for manufacturing.


## Publication

The public repository is [Baralabite/uberclicker](https://github.com/Baralabite/uberclicker). The header and footer link directly to it, including a visible source link at mobile widths. Project copy now describes an early, noncommercial, open-source project and retains the visible Design preview indicator and planned-feature caveats. The project uses MIT. The footer links the root license; “noncommercial” describes the project rather than limiting downstream use. MIT permits commercial reuse.

`.github/workflows/pages.yml` runs the small integrity/behavior check and JavaScript syntax checks on Node 24, uploads only `website/dist`, and deploys to GitHub Pages on pushes to main or manual dispatch. The live public URL is <https://baralabite.github.io/uberclicker/>. No custom-domain CNAME is included.

Production verification on 29 October 2026 confirmed all 12 published files returned HTTP 200 and matched source bytes under `/uberclicker/`. The real Three.js model rendered and visibly responded to pointer movement. Desktop (1280 × 720) and mobile (390 × 844) had no horizontal overflow or browser console warnings/errors; the sample actions, connection selection and reset disclosure worked. Production screenshots were inspected and saved in ignored `website/qa/`. See [deployment.md](deployment.md) for workflow evidence and domain status.
