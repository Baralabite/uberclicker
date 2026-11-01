import { capabilities } from "./capabilities";
import { desktop } from "./native";
import { Haptics, ImpactStyle } from "@capacitor/haptics";
import { destination, storedDestinations, SenderClient, type Destination, type SenderState } from "./sender-client";
import "./sender.css";

const STORAGE = "uberclicker_cloud_destinations_v1";

export function mountSender(root: HTMLDivElement) {
  let destinations: Destination[] = [], selected = "", busy = false, nativeActive = true, initialized = false, paired = false;
  let invitation: string | null = null, invitedTarget: Destination | undefined, registrationVersion = 0;
  let error = "", reconnect: ReturnType<typeof setTimeout> | undefined;
  let live: SenderState = { connected: false, busy: false, receivers: 0, message: "Connecting…" };
  const current = () => destinations.find(d => d.room === selected);
  const foreground = () => !root.hidden && nativeActive && document.visibilityState !== "hidden";
  const client = new SenderClient(state => {
    live = state;
    if (state.linked !== undefined) paired = state.linked;
    // Saved legacy connections have no account-link message.
    else if (state.connected && state.receivers > 0) paired = true;
    if (state.unlinked) { paired = false; error = "Clicker unlinked. Retry to get a new pairing code."; }
    update();
  });
  root.innerHTML = `<main class="sender-page" aria-label="Cue">
    <div id="link-code-panel"><output id="link-code" aria-label="Pairing code" aria-live="polite">…</output><p id="sender-error" role="alert" hidden></p><button id="sender-retry" type="button" hidden>Retry</button></div>
    <div class="cue-buttons" aria-label="Presentation controls" hidden>
      <button type="button" data-click="next" aria-label="Next" aria-describedby="send-message"><svg viewBox="0 0 64 64" aria-hidden="true"><path d="M12 8 52 32 12 56Z"/></svg></button>
      <button type="button" data-click="previous" aria-label="Previous" aria-describedby="send-message"><svg viewBox="0 0 64 64" aria-hidden="true"><path d="M52 8 12 32 52 56Z"/></svg></button>
    </div><p id="send-message" class="sr-only" role="status" aria-live="polite"></p>
  </main>`;
  const codePanel = root.querySelector<HTMLElement>("#link-code-panel")!;
  const code = root.querySelector<HTMLElement>("#link-code")!;
  const controls = root.querySelector<HTMLElement>(".cue-buttons")!;
  const status = root.querySelector<HTMLElement>("#send-message")!;
  const failure = root.querySelector<HTMLElement>("#sender-error")!;
  const retry = root.querySelector<HTMLButtonElement>("#sender-retry")!;
  function update() {
    if (!status) return;
    status.textContent = live.message;
    codePanel.hidden = paired; controls.hidden = !paired;
    code.textContent = live.pairingCode ? `${live.pairingCode.slice(0, 4)} ${live.pairingCode.slice(4)}` : "…";
    code.hidden = !!error;
    failure.hidden = !error; failure.textContent = error;
    retry.hidden = !error; retry.disabled = busy;
    for (const button of root.querySelectorAll<HTMLButtonElement>("[data-click]"))
      button.disabled = busy || live.busy || !live.connected || !live.receivers || !foreground();
    if (reconnect && (invitation || error || live.connected || live.connecting || !foreground())) { clearTimeout(reconnect); reconnect = undefined; }
    if (!reconnect && !invitation && !error && current() && foreground() && navigator.onLine && !busy && !live.connected && !live.connecting && !live.unlinked)
      reconnect = setTimeout(() => { reconnect = undefined; connect(); }, 3000);
  }
  async function persist() {
    if (!capabilities.rememberRemoteDestinations) return;
    const { SecureStorage, KeychainAccess } = await import("@aparajita/capacitor-secure-storage");
    await SecureStorage.set(STORAGE, { v: 1, destinations, selected }, false, false, KeychainAccess.whenUnlockedThisDeviceOnly);
  }
  function connect() {
    const target = current();
    if (target && foreground() && navigator.onLine) client.connect(target);
  }
  async function start() {
    if (busy || !foreground()) return;
    const version = registrationVersion;
    busy = true; error = ""; update();
    try {
      if (!initialized && capabilities.rememberRemoteDestinations) {
        const { SecureStorage } = await import("@aparajita/capacitor-secure-storage");
        const data = await SecureStorage.get(STORAGE, false, false);
        if (data !== null) {
          const saved = data as Record<string, unknown>;
          if (saved.v !== 1) throw Error("Saved connection could not be read. Unlock your phone and retry.");
          destinations = storedDestinations(saved.destinations);
          selected = destinations.find(d => d.room === saved.selected)?.room ?? destinations[0]?.room ?? "";
        }
      }
      initialized = true;
      if (live.unlinked) { destinations = destinations.filter(d => d.room !== selected); selected = ""; await persist(); }
      if (invitation || !current()) {
        const apiRoot = !desktop && capabilities.accountInApp && ["app.uberclicker.com", "localhost", "127.0.0.1"].includes(location.hostname) ? "" : "https://api.uberclicker.com";
        const name = capabilities.rememberRemoteDestinations ? "Phone clicker" : "Web clicker";
        if (invitation && !invitedTarget) {
          const random = (size: number) => Array.from(crypto.getRandomValues(new Uint8Array(size)), b => b.toString(16).padStart(2, "0")).join("");
          invitedTarget = { room: random(16), token: random(32), name };
        }
        const response = await fetch(`${apiRoot}/v1/clickers`, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name, ...(invitation ? { invite: invitation, id: invitedTarget!.room, token: invitedTarget!.token } : {}) }) });
        const data = await response.json();
        if (version !== registrationVersion) return;
        if (!response.ok) throw Error(data.error || "Could not get a pairing code. Check your connection and retry.");
        const target = destination(JSON.stringify(data), name);
        const previous = destinations, previousSelected = selected;
        destinations = [...destinations.filter(d => d.room !== target.room), target]; selected = target.room;
        try { await persist(); } catch { destinations = previous; selected = previousSelected; throw Error("Could not save this clicker. Unlock your phone and retry."); }
        invitation = null; invitedTarget = undefined;
      }
      connect();
    } catch (e) { error = e instanceof Error ? e.message : "Could not connect. Please retry."; }
    finally { busy = false; update(); if (version !== registrationVersion) void start(); }
  }
  retry.onclick = () => void start();
  for (const button of root.querySelectorAll<HTMLButtonElement>("[data-click]")) button.onclick = () => {
    if (button.disabled || !foreground()) return;
    void Haptics.impact({ style: ImpactStyle.Light }).catch(() => {});
    void client.click(button.dataset.click as "previous" | "next").catch(e => {
      live.message = e instanceof Error ? e.message : "Delivery is unconfirmed."; update();
    });
  };
  function lifecycle() {
    if (!foreground() || !navigator.onLine) client.disconnect("Remote control paused while offline or in background.");
    else if (!live.connected && !live.connecting) void start();
    update();
  }
  document.addEventListener("visibilitychange", lifecycle);
  window.addEventListener("offline", lifecycle);
  window.addEventListener("online", lifecycle);
  window.addEventListener("uberclicker:navigate", event => {
    const invite = (event as CustomEvent<{ invite: string | null }>).detail?.invite;
    if (invite) { invitation = invite; invitedTarget = undefined; registrationVersion++; paired = false; client.disconnect(); }
    lifecycle();
  });
  window.addEventListener("pagehide", () => { client.disconnect(); if (reconnect) clearTimeout(reconnect); reconnect = undefined; });
  if (capabilities.rememberRemoteDestinations) void import("@capacitor/app").then(({ App }) => App.addListener("appStateChange", state => { nativeActive = state.isActive; lifecycle(); }));
  update();
}
