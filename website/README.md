# UberClicker website preview

The approved marketing design is preserved in `dist/index.html`, with experimental
USB installation at `/flash/`, the real desktop browser USB serial configuration app at `/app/`,
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
- The procedural mesh and fallback SVG were authored for this preview. Manufacturer manuals, photographs and scraped pages are not redistributed.
- esptool-js 0.7.0 (Apache-2.0) and pako 2.1.0 (MIT/Zlib): `dist/flash/vendor/`.
- App dependency notices and the project license are copied with the app bundle.
