# UberClicker website preview

An early, buildless project and design preview. The entry is `dist/index.html`. All runtime assets, fonts and Three.js are local; opening as `file://` will not load JavaScript modules.

From the repository root:

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory website/dist
```

Open <http://127.0.0.1:4173>. Stop the server with Ctrl+C.

Run the small behavior/integrity check with:

```sh
node website/check.mjs
```

No build or dependency installation is required. The preview does not connect to hardware or accounts. No analytics or backend is included. The preview describes planned capabilities; it does not implement firmware, apps or hardware connections.

Source: <https://github.com/Baralabite/uberclicker>. This is a noncommercial project released under the MIT license. “Noncommercial” describes the project, not a restriction on downstream reuse; MIT permits commercial reuse.

GitHub Pages deploys `website/dist` through `.github/workflows/pages.yml` after integrity and JavaScript syntax checks. The live public URL is <https://baralabite.github.io/uberclicker/>, verified on 29 October 2026. No custom-domain CNAME is configured. See `docs/deployment.md` for production verification evidence.

See `docs/website-design.md` for decisions, sources, checks and known limits. `qa/` contains browser review screenshots; it is outside the served directory.

## Included third-party assets

- Three.js 0.170.0, MIT: `dist/vendor/THREE-LICENSE.txt`.
- Barlow and Barlow Condensed, SIL Open Font License: `dist/assets/Barlow-OFL.txt` and `Barlow-Condensed-OFL.txt`.
- The procedural mesh and fallback SVG were authored for this preview. Manufacturer manuals, photographs and scraped pages are not redistributed.
