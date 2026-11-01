# Cloud Channels and Client Companions

Clickers publish to a Cloud Channel; Client Companions subscribe to it. Each Companion currently listens to one channel at a time. Existing `room` fields, storage keys and API paths remain for compatibility.

The TX connects over Wi-Fi to `wss://api.uberclicker.com`. The Tauri companion
receives button events and submits keyboard presses and/or OSC messages on the
computer. The relay is deployed in the info@qcecollective.com Cloudflare account.
Firmware still requires a physical hardware test; compile/host checks do not
establish Wi-Fi, TLS, input or latency performance on a real M5.

## Connect

1. Sign in at [app.uberclicker.com](https://app.uberclicker.com) with an emailed sign-in link.
2. Create a Cloud Channel, save its one-time Client Companion code, and activate its plan. Billing is per channel: AUD $25/year, with the first month free.
3. On an M5, use **Configure** to save Wi-Fi and enable Cloud actions. The clicker connects and displays an eight-character code. On a phone or browser, open **Cue** to display a code automatically.
4. In **Cloud → Cue Remote → Add**, enter that code. It expires after ten minutes and can be claimed only once. Linking associates the clicker with your account; it does not start a subscription.
5. Tap a clicker in the **Cue Remote** table, tick the channels it should send to, and **Save**. **Unlink** is in the same dialog. Use **Add Channel** above the channels table to create another channel; tap a channel to rename or revoke it.
6. In each Client Companion's **Listen**, paste the channel's Client Companion code while output is paused. Save keyboard/OSC mappings, then choose **Enable output**. Up to eight Companions can listen to a channel. Each Companion listens to one channel at a time.

Each channel’s **Manual** link opens a public cue display at `/manual/{channel-id}`.
Share its address; no login is needed.
The view is a blank white canvas with **Standby** in the center. A forward click
flashes the whole canvas green with a right-pointing triangle for 700 ms; a back
click flashes red with a left-pointing triangle. Sound starts off. **Sound on** enables a high
forward tone and a low back tone for that open view. Keep the page visible: hidden pages pause reception and
reconnect without replay when shown again.

Anyone with the Manual link can see the channel name and live cues, but cannot
publish clicks or access account settings or pairing credentials. An active channel
plan is still required; revoking the channel disconnects its public views too.
Manual views share the eight-listener limit with Client Companions. Their receipt
acknowledgement means the cue was displayed, not that an operator advanced a slide.
The public `/v1/rooms/{id}/manual` endpoint returns only the channel name on GET
and accepts receive-only WebSockets with the usual sync, arm and ack messages.

Manual publisher codes remain available for older clients. Account-linked clickers have their own stored credentials, independent of channel subscriptions. **Unlink** revokes a clicker's credentials. On a phone or browser, choose **Retry** in Cue to get a new pairing code; an offline clicker receives the unlink notification when it reconnects. On hardware, reset its account connection before linking it again.

Alternatively, choose **Copy link** on a channel and open the signed link on the browser that will send clicks. It links that clicker to your account and the selected channel, then opens Cue automatically. Links expire after ten minutes and can register one clicker. Retrying with the same clicker credentials safely recovers a lost response. Treat an unused link as private.

The Cue Remote table shows **Last seen**, newest first. It refreshes while the portal is visible and no form is being edited. Browser heartbeats contribute to presence; M5 activity reflects its latest connection, clock sync, click, or disconnect.

The new linking flow uses migrations `0003_clickers.sql` and `0004_clicker_links.sql`, the `CLICKERS` Durable Object binding/migration, and matching Worker/app/firmware releases. The migrations, Worker and web app were deployed on 1 November 2026. Live registration, pairing and recovery after offline revocation passed smoke checks. No hardware was flashed; native apps require installation of the updated builds.

Each channel has its own subscription and calendar-month trial. Payment method required; subscriptions renew automatically. The top-right avatar contains **Edit display name**, **Billing** and **Sign out**. Billing opens Stripe's portal in a new tab for payment details and cancellation at period end. **Revoke** immediately disconnects the channel, expires unfinished checkouts and stops renewal. Pairing tokens cannot be recovered after their initial display; revoke and replace a channel if its codes are lost or exposed.

Accounts and sessions use Better Auth with Cloudflare D1. Cloudflare Email Service sends sign-in, verification and reset links. Passwords require at least 12 characters, email verification is required, and reset revokes existing sessions. Google sign-in is implemented but disabled until a Google OAuth client is configured; it is not advertised as available.

The companion enables output automatically after connecting to its saved channel on
startup. In **Startup settings**, turn off **Enable output on startup** to start paused;
the preference is saved, including across upgrades. New channels stay paused while
you pair and configure them. Output still pauses on every disconnect and must be
enabled manually afterwards.

**Start on boot** is off by default. Enable it under **Startup settings** to launch Companion
when you sign in on macOS, Windows, or Linux. This uses a macOS LaunchAgent,
Windows user startup registration, or a Linux XDG desktop autostart entry through
the [Tauri autostart plugin](https://v2.tauri.app/plugin/autostart/). It runs in your
desktop session, not before login. Install the app in its permanent location first;
if you move it, turn Start on boot off and on again to update the launch path.
Linux requires a desktop session supporting XDG autostart. The startup output
preference applies equally to automatic and manual launches.

Closing its window
leaves it in the menu bar/system tray. Use its **Pause output** or **Quit** menu
items when finished. The app needs an active desktop session. Computer sleep
and disconnected networks cannot deliver cues.

OSC supports one message per button, with one signed int32 argument and an address
containing ASCII letters/digits, `/`, `_` and `-` (at most 64 bytes). Hosts are IP
addresses or `localhost`. Keyboard supports common presentation/navigation keys
and Ctrl/Alt/Shift/Cmd-Win modifiers. macOS needs Accessibility permission.
Windows cannot inject into applications with a higher integrity level. Linux X11
is supported by the chosen input library; Wayland input remains experimental and
requires desktop-specific validation. The UI must not claim arbitrary Wayland
desktops are supported for keyboard injection. OSC does not use input injection.

## Idle behavior and cost

Idle WebSockets stay connected using Durable Object hibernation. The companion
sends `ping`, answered by Cloudflare's built-in `setWebSocketAutoResponse`; the TX
uses protocol Ping/Pong. Neither heartbeat wakes the object or incurs idle-duration
charges. Clickers make no periodic cloud clock requests or idle database writes.
An open account portal refreshes presence every thirty seconds, reading the latest heartbeat/activity timestamp from each linked clicker's Durable Object. These reads can wake objects; a closed portal does not poll. Unclaimed connected clickers renew their pairing code every ten minutes. A single entitlement-expiry alarm closes sockets when paid access ends.

A real click holds the room awake for ten minutes after the last click; expiration
removes the keep-awake timer so Cloudflare may hibernate it. Being armed does not
keep a room awake. Ordinary heartbeats and clock sync do not renew that window.
At the paid duration rate, a full isolated ten-minute window consumes 76.8 GB-s,
about US$0.00096 before included allowances and billing rounding. Usage is per room,
not per connected receiver.

Cloudflare's Free plan can run this relay within its daily quotas, including
13,000 GB-s/day of Durable Object duration. It stops serving over-quota operations;
production activity across thousands of rooms will need capacity planning and may
need the paid plan. Paid Workers has a US$5/month minimum even when all rooms idle.
Room credentials occupy a few KB of durable storage, covered by the current 5 GB
included allocation. Creation, reconnects, real messages and initialization are
activity and can consume quotas. An idle connection has no per-minute fee.
See [Cloudflare's billing](https://developers.cloudflare.com/durable-objects/platform/pricing/).

After a gap of over five minutes, the next click resynchronizes the TX and receiver
clocks using the existing connection. This adds a round trip to the first click
after a long idle, avoids idle clock traffic, and preserves the 750 ms expiry.
There is no fixed latency guarantee; measure cold and warm p50/p95/p99 separately.

## Relay protocol 1

`POST /v1/rooms` creates a random 128-bit room ID and independent random 256-bit
publisher/receiver capabilities. Creation requires a verified account, is limited to five requests
per minute per user, and permits 20 lifetime channels per account. New channels cannot relay until their subscription/trial is active. The service stores only SHA-256 token hashes; the companion
stores its capabilities in Keychain / Credential Manager / Secret Service. Ordinary
settings files contain mappings and the startup output preference. The TX stores its publisher capability in
the existing NVS record; physical flash extraction is outside its confidentiality
guarantees. Pairing codes contain `{v:1,room,token}` with only the publisher token.

Connect to `/v1/rooms/{room}/tx` or `/client` with `Authorization: Bearer {token}`
in the WebSocket upgrade headers. Credentials never go into URLs. There are at most eight TX connections
and eight listening clients per channel. Neither role can use the other role's token.
Rooms are independent Durable Objects with an Oceania placement hint. Global
deployments should choose placement based on the endpoints' locations.

An account clicker reconnecting with its valid but revoked token receives a
WebSocket close with code `1008` and reason `Clicker unlinked`, matching live
revocation. It receives no ready message or usable session. Unknown identities
and incorrect tokens still fail the upgrade with HTTP 401. This lets browsers
recover after offline revocation without exposing authentication response bodies.

The relay sends `{v:1,type:"ready",session,now,receivers}`. A clock request
`{v:1,type:"sync",nonce}` receives `{v:1,type:"sync",nonce,now}` with Unix milliseconds.
Endpoints use monotonic local clocks and the midpoint of a round trip no longer
than 500 ms. Clients send `{v:1,type:"arm",armed:true|false}` to opt into delivery.
TX frames are `{v:1,type:"click",seq,button:"previous"|"next",sentAt,transmitterId}` with strictly
increasing uint32 sequence numbers scoped to the relay-issued connection session.
`transmitterId` is the device's stable, lowercase 12-digit eFuse ID. It is optional
for older firmware, but unidentified clicks are rejected in ProPresenter mode.
This ID is reported by the TX, not a separate authentication credential: all TXs
with the room's publisher token are trusted. Presentation mappings prevent
accidental control, not impersonation by someone who holds that token.

The server rejects clicks older than 750 ms or over 100 ms in the future. It
forwards `{v:1,type:"click",id,transmitterId,button,sentAt,expiresAt}` only to armed, connected
receivers. `id` is `session:seq`. Receivers reject duplicates, stale events, and
events invalidated by pause/reconnect/mapping changes, including before native
output. Clicks are never retained for offline delivery or replayed on reconnect.

The companion responds `{v:1,type:"ack",id,status}` with `submitted`, `rejected`,
or `unknown`. The relay forwards each listener’s correlated `result` to the TX. Current clickers display the first listener response; it does not confirm that all listeners acted. Check each output when delivery is uncertain. Submitted means
the local API accepted the output, not that PowerPoint/QLab/etc. acted. Lost ACKs
remain uncertain; no automatic cue retry is safe. Both keyboard and OSC can be
enabled, so partial output may be Unknown. Message limits are 512 bytes and 40
application messages/second per connection. Secrets and click payloads are not
logged by the worker.

## Build and deploy

```sh
cd cloud
npm ci --prefix ../app
npm run build --prefix ../app
npm ci
npm run check
npm test
npm run deploy

cd ../companion
npm ci --prefix ../app
npm ci
npm run tauri dev
npm run tauri build
```

Rust stable and the platform Tauri prerequisites are required. Windows uses
WebView2; Linux uses WebKitGTK and a running Secret Service credential store.
The macOS build produces `.app` and `.dmg`; native Windows/Linux runners produce
their installers. macOS signing/notarization and Windows signing require the
release owner's certificates. No signing identity is configured here. The Cloudflare account uses Workers Paid for outbound login email.

The **Cloud and desktop companion** GitHub Actions workflow tests and packages
the desktop app on pull requests and relevant pushes to `main`. After all cloud
and desktop checks pass on `main`, it publishes a GitHub prerelease tagged
`companion-build-<run ID>-<attempt>`. You can also publish by running the workflow
manually on `main`; dispatches on other branches only build and test. Each release
records its source commit and contains a universal macOS `.dmg` (Apple silicon
and Intel), a Windows x64 `.exe`, and Linux x64 `.deb` and `.AppImage` installers.
CI artifacts remain available for each platform. A draft becomes public only
after all four installers have uploaded successfully.

Download from the main page's **Downloads** section or
[Companion releases](https://github.com/Baralabite/uberclicker/releases?q=companion-build).
Expand **Assets** on the newest Companion preview. These builds are unsigned and
macOS builds are not notarized, so the OS may block installation or show a publisher
warning. Companion releases are separate from firmware prerelease tags and are
not offered by the firmware installer or OTA feed.

Both targets use `app/src`: the shared shell, device editor, receiver UI and serial
protocol. Tauri embeds `app/dist`; `companion` contains only the desktop build
wrapper and Rust services. Browser USB uses Web Serial, while desktop USB uses
Rust `serialport` with the same TypeScript framing, bounds and request correlation.
Switching sections preserves edits and receiver state. Cloud reception and native
output continue in Rust independently of the displayed section or hidden window.

`cloud/wrangler.jsonc` publishes the relay at `api.uberclicker.com` and the shared app/account API at `app.uberclicker.com`. The older `/setup/` entry remains compatible. D1 is read on connection/account activity, not on each click. The billing Durable Object serializes checkout, revocation and webhook reconciliation per account. Webhooks verify Stripe signatures and reread current subscriptions, so delayed events cannot restore canceled access.

For a fresh deployment, apply D1 migrations with `npx wrangler d1 migrations apply uberclicker-accounts --remote`, then set encrypted secrets `BETTER_AUTH_SECRET`, `STRIPE_SECRET_KEY` and `STRIPE_WEBHOOK_SECRET` using `wrangler secret put`. Keep credentials out of source control. The annual Stripe price and fixed application origin are in `wrangler.jsonc`. Checkout offers only AUD $25/year per channel. The archived monthly price remains configured solely for legacy subscription reconciliation; it cannot be selected for a new checkout. The live restricted key has customer, Checkout, subscription and billing portal write access and price read access.

The Stripe webhook is `https://app.uberclicker.com/api/billing/webhook`, listening to Checkout completion, subscription created/updated/deleted/paused/resumed and invoice paid/payment_failed. Enable email sending for `uberclicker.com` in Cloudflare Email Service before setting `EMAIL_ENABLED=true`.

To finish Google sign-in, create a Google web OAuth client with callback `https://app.uberclicker.com/api/auth/callback/google`, set `GOOGLE_CLIENT_ID` and `GOOGLE_CLIENT_SECRET` as Worker secrets, and complete Google's consent/publishing configuration. The dashboard enables Google only when both credentials exist.

Verified on 1 November 2026: local password/session and relay integration tests; destination isolation, revocation and billing reconciliation tests; user-confirmed live email sign-in and Stripe Checkout. A completed paid charge, renewal, physical hardware and live phone-to-native output have not been validated by this deployment task.
