import "./install.css";
import { desktop } from "./native";
import { nativeInstaller } from "./native-flash";

export function mountInstall(root: HTMLDivElement) {
  if (desktop) window.uberclickerInstaller = nativeInstaller;
  root.innerHTML = `<main class="install-page" aria-labelledby="install-title">
    <h1 id="install-title">Install</h1>
    ${desktop ? '' : `<section aria-labelledby="companion-downloads-title">
      <h2 id="companion-downloads-title">Companion software</h2>
      <p>Receive Cloud Channel cues as keyboard and OSC actions on your computer.</p>
      <ul class="companion-downloads">
        <li><a href="https://github.com/Baralabite/uberclicker/releases?q=companion-build"><strong>Download for macOS</strong><span>Apple silicon &amp; Intel · .dmg</span></a></li>
        <li><a href="https://github.com/Baralabite/uberclicker/releases?q=companion-build"><strong>Download for Windows</strong><span>x64 · .exe installer</span></a></li>
        <li><a href="https://github.com/Baralabite/uberclicker/releases?q=companion-build"><strong>Download for Linux</strong><span>x64 · .deb or .AppImage</span></a></li>
      </ul>
      <p class="hint">Links open GitHub Releases. Choose the latest Companion preview and expand Assets for your installer. Preview builds are unsigned; your operating system may show a security warning or block installation.</p>
    </section>`}
    <section aria-labelledby="device-flashing-title"><h2 id="device-flashing-title">Device flashing</h2>
      <iframe class="firmware-installer" title="UberClicker firmware installer" src="./installer/" allow="serial" loading="lazy"></iframe>
    </section>
  </main>`;
  const frame = root.querySelector<HTMLIFrameElement>("iframe")!;
  window.addEventListener("message", event => {
    if (event.origin !== location.origin || event.source !== frame.contentWindow || event.data?.type !== "uberclicker:installer-size") return;
    const height = event.data.height;
    if (Number.isFinite(height) && height >= 200 && height <= 10000) frame.style.height = `${Math.ceil(height) + 2}px`;
  });
}
