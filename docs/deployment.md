# Website deployment

The public repository is [Baralabite/uberclicker](https://github.com/Baralabite/uberclicker). The website was deployed and verified at [GitHub Pages](https://baralabite.github.io/uberclicker/) on 29 October 2026. The user then confirmed preserving **uberclicker.com** for domain hookup; the Pages address now redirects there. Custom-domain DNS propagation and HTTPS remain pending. Device design remains under review. The project operates noncommercially under the MIT license.

GitHub Actions publishes the static `website/dist` site on pushes to `main`, using the repository’s Pages deployment environment. Settings → Pages → Source is configured as **GitHub Actions**, with custom domain **uberclicker.com**. HTTPS enforcement awaits GitHub’s certificate. This website workflow does not build firmware, create firmware releases, or provide a functional USB flasher. Those remain pending, along with apps and measured dock CAD.

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
