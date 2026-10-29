# UberClicker website preview

The approved marketing design is preserved in `dist/index.html`, with experimental
USB installation at `/flash/`, the real browser BLE configuration app at `/app/`,
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
```

Before serving, run `npm ci --prefix app` and `node scripts/build-website.mjs` to
copy the built app and device preview. The homepage demonstrations remain labelled
simulations. The separate setup/installer pages can connect to hardware only by
explicit user action in supported secure-context browsers. No analytics, account
backend or remote relay is included. Protected LAN/hotspot configuration is unavailable.

Source: <https://github.com/Baralabite/uberclicker>. This is a noncommercial project released under the MIT license. “Noncommercial” describes the project, not a restriction on downstream reuse; MIT permits commercial reuse.

Pages builds the app, runs integrity/behavior checks and mirrors verified published
firmware through `.github/workflows/pages.yml`. Firmware CI uses a separate workflow.
The original site was verified at <https://baralabite.github.io/uberclicker/> before
the user confirmed preserving `uberclicker.com`. GitHub now reports valid DNS, while
HTTPS certificate issuance remains pending. Hosted BLE/Serial requires HTTPS.
No source `CNAME` is needed for Actions deployment. See [deployment](../docs/deployment.md)
and [installation](../docs/flashing.md) for evidence and recovery.

See `docs/website-design.md` for decisions, sources, checks and known limits. `qa/` contains browser review screenshots; it is outside the served directory.

## Included third-party assets

- Three.js 0.170.0, MIT: `dist/vendor/THREE-LICENSE.txt`.
- Barlow and Barlow Condensed, SIL Open Font License: `dist/assets/Barlow-OFL.txt` and `Barlow-Condensed-OFL.txt`.
- The procedural mesh and fallback SVG were authored for this preview. Manufacturer manuals, photographs and scraped pages are not redistributed.
- esptool-js 0.7.0 (Apache-2.0) and pako 2.1.0 (MIT/Zlib): `dist/flash/vendor/`.
- App dependency notices and the project license are copied with the app bundle.
