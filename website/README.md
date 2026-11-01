# UberClicker website preview

The approved marketing design is preserved in `dist/index.html`, with USB installation in the web app at `https://app.uberclicker.com/#install`, the real desktop browser USB serial configuration app at `/app/`,
and firmware-derived device review at `/device-preview.html`. Fonts, Three.js and
the pinned esptool-js bundle are local. Opening as `file://` does not load modules.

From the repository root:

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory website/dist
```

Open <http://127.0.0.1:4173>. Stop the server with Ctrl+C.

Run the small behavior/integrity check with:

```sh
node website/check.mjs
node --test website/ui.test.mjs website/flash.test.mjs
```

The connection diagram has Simple and Advanced views. Pro pricing converts the
AUD $25 annual charge using [Frankfurter reference rates](https://frankfurter.dev/).
The Worker supplies a visitor country without caching it; static previews fall
back to the browser region. Visitors can also choose a currency. Unsupported
regions or failed rate requests retain the AUD price. Checkout remains in AUD.

Before serving, run `npm ci --prefix app` and `node scripts/build-website.mjs` to
copy the built app and device preview. The homepage demonstrations remain labelled
simulation. The separate setup/installer pages can connect to hardware only by
explicit user action in supported secure-context browsers. The Cloudflare account dashboard and Pro relay are deployed separately at `app.uberclicker.com` and `api.uberclicker.com`. Protected LAN/hotspot configuration is unavailable.

Source: <https://github.com/Baralabite/uberclicker>. Local functionality and source remain free under MIT; Cloud Channels are an optional paid Pro feature.

Production is served by the `uberclicker-website` Cloudflare Worker, using `wrangler.jsonc`. Deploy from the repository root after building with:

```sh
./cloud/node_modules/.bin/wrangler deploy --config website/wrangler.jsonc
```

Cloudflare proxies the apex/www DNS records and routes their traffic to the Worker. Both domains have working HTTPS. `/app/` redirects to the app's device configuration section; the homepage Login button opens the account dashboard. The existing GitHub Pages workflow remains a legacy mirror/build check; it does not deploy updates to Cloudflare. See [deployment](../docs/deployment.md) for the current manual release commands and historical Pages evidence.

See `docs/website-design.md` for decisions, sources, checks and known limits. `qa/` contains browser review screenshots; it is outside the served directory.

## Included third-party assets

- Three.js 0.170.0, MIT: `dist/vendor/THREE-LICENSE.txt`.
- Barlow and Barlow Condensed, SIL Open Font License: `dist/assets/Barlow-OFL.txt` and `Barlow-Condensed-OFL.txt`.
- The procedural mesh and fallback SVG were authored for this preview.
- Coming Soon hardware previews: `dist/assets/hardware/lilygo-t-beam-supreme.jpg` is the [official LILYGO product photograph](https://lilygo.cc/products/t-beam-supreme), retrieved 2026-11-01 from its [image CDN](https://lilygo.cc/cdn/shop/files/LILYGO-T-BEAM_10_3bb84be5-da09-4626-8b93-99be997d49b8.jpg?v=1739257071&width=1000); image rights remain with LILYGO. `charging-dock.png` is copied from the project's `cad/previews/dock-concept-2-bay.png`. Both are greyed out with CSS; refresh the dock copy when its render changes.
- esptool-js 0.7.0 (Apache-2.0) and pako 2.1.0 (MIT/Zlib): `dist/flash/vendor/`.
- App dependency notices and the project license are copied with the app bundle.

### Presentation software marquee

The homepage logo strip uses local assets in `dist/assets/software/`, displayed
in greyscale with CSS. It scrolls continuously across the full viewport width;
reduced motion displays one wrapping, stationary set. The second animated set is hidden
from assistive technology. No JavaScript or dependencies are needed for the marquee.

Logo sources (retrieved 2026-11-01; trademarks remain with their respective owners):

- `powerpoint.svg`: [Microsoft PowerPoint via Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Microsoft_Office_PowerPoint_(2025%E2%80%93present).svg), Microsoft application icon.
- `propresenter.svg`: [ProPresenter](https://www.propresenter.com/), official navigation wordmark.
- `easyworship.svg`: [EasyWorship](https://easyworship.com/), official inline wordmark, with its styles bundled and SVG viewBox casing normalized for standalone use.
- `proclaim.svg`: [Proclaim](https://proclaim.logos.com/), official navigation wordmark.
- `openlp.png`: [OpenLP](https://openlp.org/assets/images/logo.png), official application icon.
- `bitfocus-companion.png`: [Bitfocus Companion](https://bitfocus.io/companion), official product icon from `/assets/products/companion/companion.png`.
- `freeshow.png`: [FreeShow](https://freeshow.app/images/favicon.png), official application icon.

## Hero previews and footer

The hero shows the procedural ThinkNode M5 and iPhone models side by side, with
local SVG fallbacks when WebGL is unavailable. The pair pivots subtly around its
shared centre with pointer movement. `python3 scripts/build-hero-screen.py`
compiles the real firmware renderer and updates `assets/m5-tx-screen.png` and its
embedded fallback with example name Joe Presenter. The iPhone Cue illustration
matches the paired state in `app/src/sender.ts`, `sender.css` and `main.ts`; it
sends no commands. Both models respect reduced-motion preferences.

The dark accent footer links to GitHub Issues, the MIT license and `privacy.html`.
The privacy contact uses the existing project account address documented in
`docs/cloud.md`: `info@qcecollective.com`.

## Web app installation

Install is a browser-only page (`#install`), containing Companion download links
and the USB installer. `app/scripts/installer.mjs` copies the existing flasher,
release index, vendored dependencies and styles into the ignored
`app/public/installer/` directory before development and builds. The installer
runs in a same-origin iframe so its IDs and USB workflow remain isolated from
configuration. The embedding grants serial access but does not select a device
or write firmware automatically. Run release mirroring before the app build to
include the latest verified releases. The website Worker redirects old `/flash/`
entry links to the web app; the installer source remains here for release tools.

The Companion download CTA uses Apple and Linux icons from Simple Icons v13 (CC0), with a Windows pane icon drawn as SVG.

ThinkNode M5 product photo: [ameriDroid product listing](https://ameridroid.com/products/thinknode-m5-meshtastic-radio-1), image MSD2573.jpg (retrieved 2026-11-02).
