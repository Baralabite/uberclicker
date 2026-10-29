# Website deployment

The public repository is [Baralabite/uberclicker](https://github.com/Baralabite/uberclicker). The website is live at [GitHub Pages](https://baralabite.github.io/uberclicker/), verified on 29 October 2026. Device design remains under review. The project operates noncommercially under the MIT license.

GitHub Actions publishes the static `website/dist` site on pushes to `main`, using the repository’s Pages deployment environment. Settings → Pages → Source is configured as **GitHub Actions**, with HTTPS enforced and no custom domain. This website workflow does not build firmware, create firmware releases, or provide a functional USB flasher. Those remain pending, along with apps and measured dock CAD.

## Deployment verification

- [Initial deployment run](https://github.com/Baralabite/uberclicker/actions/runs/40829738216) completed successfully.
- All 12 published files, including HTML, CSS, JavaScript, Three.js, fonts and the fallback illustration, returned HTTP 200 under `/uberclicker/` and matched the local source bytes.
- The live Three.js model rendered and responded to pointer movement. Header/footer repository links and the MIT license link point to the public repository.
- Desktop (1280 × 720) and mobile (390 × 844) browser checks found no horizontal overflow or console warnings/errors. Sample Previous/Next actions, connection selection and the factory-reset disclosure worked.
- Production captures are saved locally in ignored `website/qa/`: `production-desktop.jpg`, `production-model-pointer.jpg` and `production-mobile.jpg`.

A custom-domain setting found during verification redirected visitors to the parked domain. It was cleared to keep the `github.io` address serving the site until domain setup is requested. DNS records were not changed.

## Connect uberclicker.com

No DNS changes have been made. Follow [GitHub’s custom-domain guide](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site):

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
