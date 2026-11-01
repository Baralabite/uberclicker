# Website deployment

## Current Cloudflare production — 1 November 2026

- Marketing: [uberclicker.com](https://uberclicker.com) and [www.uberclicker.com](https://www.uberclicker.com), served by `uberclicker-website`.
- App/accounts: [app.uberclicker.com](https://app.uberclicker.com), served by `uberclicker-cloud` with D1, Durable Objects and Cloudflare Email Service.
- Relay: `wss://api.uberclicker.com`; active Pro entitlement required per destination.
- Pricing: AUD $3/month or $25/year with one calendar month free. Stripe Checkout, signed webhooks and customer portal are configured. The user confirmed live magic-link sign-in and Checkout.
- The Login button links to the account app; old `/app` links redirect to device setup there.

The apex A and www CNAME records are now **proxied** through Cloudflare. Worker routes `uberclicker.com/*` and `www.uberclicker.com/*` serve the new assets without contacting the legacy GitHub origin. Preserve email DNS records. Normal HTTPS requests to both domains returned HTTP 200; certificate verification was not bypassed.

Production deployment is manual from this workspace:

```sh
npm ci --prefix app
npm ci --prefix cloud
node scripts/build-website.mjs
node website/check.mjs
npm run check --prefix cloud
npm test --prefix cloud
(cd cloud && npx wrangler d1 migrations apply uberclicker-accounts --remote && npm run deploy)
./cloud/node_modules/.bin/wrangler deploy --config website/wrangler.jsonc
```

Secrets are encrypted in the Worker, not in this repository. See [cloud setup](cloud.md) for auth/Stripe configuration. Google sign-in awaits OAuth credentials. GitHub Actions' existing Pages job still builds/mirrors legacy assets; pushing source alone does not update Cloudflare. No new CI deployment credential was created.

The simplification update was deployed on 1 November 2026 from commit `dbc7962`:

- Cloud Worker version: `a9e96e09-ffbf-4790-9e01-cb7d7277f389`.
- Website Worker version: `e9d631ea-17ec-4da5-86eb-b52768167bde`.
- D1 migrations `0003_clickers.sql` and `0004_clicker_links.sql` applied successfully.
- App, apex/www website, application bundle, installer validator and release index returned HTTP 200 and matched local bytes. Native registration preflight returned HTTP 204 with the required CORS headers.
- Live clicker registration, browser-authenticated pairing and offline-revocation recovery passed. The temporary unclaimed smoke-test record was removed.

Validation: 48 shared-app tests, 17 cloud tests, 21 desktop tests, 13 installer/mirror tests and 13 release-packaging tests passed. Firmware host/configuration/UI/OTA checks passed, and all three firmware build variants succeeded. Android debug, unsigned iOS simulator and macOS Companion packages built locally; both phone builds also passed GitHub Actions. One local OS-login-registration test remained intentionally ignored. Physical hardware, signed iPhone/store distribution, live Stripe renewal charging and signed webhook delivery after a completed subscription remain untested; Checkout itself was user-verified.

## Historical GitHub Pages deployment (superseded)

The notes below record the previous hosting setup and its old HTTPS limitation; they are not the current production configuration.


The public repository is [Baralabite/uberclicker](https://github.com/Baralabite/uberclicker). The website was deployed and verified at [GitHub Pages](https://baralabite.github.io/uberclicker/) on 29 October 2026. The user then confirmed preserving **uberclicker.com** for domain hookup; the Pages address now redirects there. Custom-domain DNS propagation and HTTPS remain pending. Device design remains under review. The project operates noncommercially under the MIT license.

GitHub Actions publishes `website/dist` on pushes to `main` and after successful
firmware publication, using the existing Pages environment and custom domain
**uberclicker.com**. The workflow builds the real browser app, runs site/flasher
checks and mirrors exact verified release assets. Firmware build/prerelease creation
uses `.github/workflows/firmware.yml`. Stable tags remain blocked pending physical
acceptance. The dock is an unmeasured fit prototype, with checked meshes.

During final integration on 29 October, GitHub's Pages health API reported both
apex and `www` DNS valid, pointed to Pages and HTTPS-eligible, with no non-Pages IP.
It still reported `responds_to_https:false` / `peer_failed_verification`; the Pages
certificate was absent and ordinary HTTPS requests failed hostname verification.
The custom domain remains preserved. Hosted Web Bluetooth/Web Serial cannot work
until trusted HTTPS is available; localhost and native development builds are
separate options. No certificate warning was bypassed.

## Experimental implementation deployment

The completed firmware/app/UI integration is published as
[v0.2.0-experimental.1](https://github.com/Baralabite/uberclicker/releases/tag/v0.2.0-experimental.1).
[Pages deployment](https://github.com/Baralabite/uberclicker/actions/runs/40851345405)
serves the real app at `/app/`, current TX review at `/device-preview.html` and
installer at `/flash/`. The homepage, app entry, review and installer source bytes
were verified through GitHub's HTTP origin with the preserved domain Host header.
The release index, versioned manifest/four images and compatible OTA feed match
published GitHub assets exactly. This verifies deployment bytes, not trusted HTTPS
or an actual device installation.

GitHub still reports `cname:uberclicker.com`, `https_certificate:null` and
`https_enforced:false`. Secure hosted BLE/Serial and device OTA remain blocked until
certificate provisioning completes. No TLS verification was bypassed. See
[integration evidence](integration-checks.md) and [installation](flashing.md).

## Deployment verification

- [Initial deployment run](https://github.com/Baralabite/uberclicker/actions/runs/40829738216) and [verification documentation deployment](https://github.com/Baralabite/uberclicker/actions/runs/40830266269) completed successfully.
- All 12 published files, including HTML, CSS, JavaScript, Three.js, fonts and the fallback illustration, returned HTTP 200 under `/uberclicker/` and matched the local source bytes.
- The live Three.js model rendered and responded to pointer movement. Header/footer repository links and the MIT license link point to the public repository.
- Desktop (1280 × 720) and mobile (390 × 844) browser checks found no horizontal overflow or console warnings/errors. Sample Previous/Next actions, connection selection and the factory-reset disclosure worked.
- Production captures are saved locally in ignored `website/qa/`: `production-desktop.jpg`, `production-model-pointer.jpg` and `production-mobile.jpg`.

A custom-domain setting initially redirected visitors to the parked domain. It was temporarily cleared for the checks above, then restored externally during DNS setup; the user explicitly confirmed preserving it. This verification session did not change DNS records.

At 16:38 AEST on 29 October 2026, authoritative DNS returned all four GitHub Pages A records below and `www` pointed to `baralabite.github.io`. Recursive resolvers disagreed: one returned the GitHub addresses while another retained `192.64.119.174`. Direct HTTP requests to GitHub’s IP with the `uberclicker.com` host returned all 12 published files (200), matching source bytes, but normal browser navigation still encountered the stale parking redirect. GitHub had no HTTPS certificate yet. Recheck normal apex/`www` navigation and enable HTTPS once propagation and certificate issuance finish.

## Connect uberclicker.com

The custom domain is saved in GitHub and authoritative DNS already matches these records; propagation and certificate issuance are still in progress. For reference, follow [GitHub’s custom-domain guide](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site):

1. Optionally verify ownership first using the exact TXT record GitHub generates; verification is recommended.
2. In the repository’s Settings → Pages, save **uberclicker.com** as Custom domain **before changing DNS**.
3. In the authoritative DNS provider, add these records, with TTL **Auto**:

| Type | Host | Value |
| --- | --- | --- |
| A | @ | 185.199.108.153 |
| A | @ | 185.199.109.153 |
| A | @ | 185.199.110.153 |
| A | @ | 185.199.111.153 |
| CNAME | www | baralabite.github.io |

The CNAME contains no protocol or repository path. Replace only conflicting parking records for `@` and `www`, including the observed `192.64.119.174` and `parkingpage.namecheap.com` values if still present. Preserve mail, TXT, and unrelated records; these values do not establish who operates the authoritative DNS.

Allow up to 24 hours for propagation. Once GitHub’s DNS check succeeds and its certificate is available, enable **Enforce HTTPS**; certificate availability can also take up to 24 hours. Verify both apex and `www` resolve and load successfully. An Actions-based deployment does not require a source `CNAME` file.
