import { capabilities } from "./capabilities";
import "./account.css";

type LinkedClicker = { id: string; name: string; channels: string[]; last_seen?: number };
type Account = { devices?: LinkedClicker[]; user: { name: string; email: string }; pro: boolean; billing: { hasCustomer: boolean; status: string; until: number; trialUsed: boolean }; rooms: { id: string; name: string; created_at: number; revoked: number; status: string; pro_until: number; trial_used: number }[] };
type Config = { email: boolean; billing: boolean };
const escape = (s: unknown) => String(s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]!);
async function api<T = Record<string, unknown>>(path: string, body?: unknown): Promise<T> {
  const response = await fetch(path, { method: body === undefined ? "GET" : "POST", credentials: "same-origin", headers: body === undefined ? {} : { "Content-Type": "application/json" }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const data = await response.json();
  if (!response.ok) throw new Error(data.message || data.error || "Request failed. Please try again.");
  return data as T;
}
export function mountAccount(root: HTMLDivElement) {
  let config: Config = { email: false, billing: false };
  let current: Account | null = null;
  let channelCodes: { tx: string; client: string } | null = null;
  function message(text: string) { (root.querySelector<HTMLElement>("dialog[open] [role=status]") ?? root.querySelector<HTMLElement>("[role=status]"))!.textContent = text; }
  async function run(action: () => Promise<void>) {
    const buttons = [...root.querySelectorAll<HTMLButtonElement | HTMLInputElement>("button, dialog[open] input")];
    const disabled = buttons.map(b => b.disabled);
    for (const button of buttons) button.disabled = true;
    message("");
    try { await action(); } catch (error) { message(error instanceof Error ? error.message : "Something went wrong. Try again."); }
    finally { buttons.forEach((button, i) => { button.disabled = disabled[i]; }); }
  }
  const on = (id: string, action: () => Promise<void>) => root.querySelector<HTMLButtonElement>(`#${id}`)?.addEventListener("click", () => void run(action));
  async function refresh(shouldApply: () => boolean = () => true) {
    const previousUser = current?.user.email;
    let next: Account | null;
    try { next = await api<Account>("/api/account"); }
    catch (e) {
      const session = await api<{ user?: unknown } | null>("/api/auth/get-session");
      if (session?.user) throw e;
      next = null;
    }
    if (!shouldApply()) return;
    current = next;
    if (current?.user.email !== previousUser) channelCodes = null;
    render();
  }
  function render() {
    renderAccountMenu();
    if (!capabilities.accountInApp || !["app.uberclicker.com", "localhost", "127.0.0.1"].includes(location.hostname)) {
      root.innerHTML = `<main class="account-page"><h1>Account</h1><p>Manage Cloud Channels and billing online.</p><a class="account-link" href="https://app.uberclicker.com/#account" target="_blank" rel="noopener">Open account</a><p>USB configuration is free and works without an account.</p></main>`; return;
    }
    root.innerHTML = `<main class="account-page${current ? "" : " account-signin"}"><p role="status" aria-live="polite" class="account-message"></p>${current ? dashboard(current) : login()}</main>`;
    if (current) {
      const linkForm = root.querySelector<HTMLFormElement>("#link-clicker")!;
      const addClicker = root.querySelector<HTMLButtonElement>("#add-clicker")!;
      const pairingPanel = root.querySelector<HTMLElement>("#clicker-pairing")!;
      addClicker.onclick = () => {
        pairingPanel.hidden = !pairingPanel.hidden;
        addClicker.setAttribute("aria-expanded", String(!pairingPanel.hidden));
        if (!pairingPanel.hidden) linkForm.querySelector<HTMLInputElement>("input")!.focus();
      };
      root.querySelector<HTMLButtonElement>("#cancel-link")!.onclick = () => {
        pairingPanel.hidden = true; linkForm.reset();
        addClicker.setAttribute("aria-expanded", "false"); addClicker.focus();
      };
      linkForm.onsubmit = e => { e.preventDefault(); void run(async () => { await api("/api/clickers/link", { code: new FormData(linkForm).get("code") }); await refresh(); message("Clicker linked. Tap it to choose its channels."); }); };
      for (const row of root.querySelectorAll<HTMLTableRowElement>("[data-clicker]")) row.onclick = () => {
        const clicker = current!.devices!.find(d => d.id === row.dataset.clicker)!;
        const dialog = root.querySelector<HTMLDialogElement>("#clicker-dialog")!;
        const channels = current!.rooms.filter(c => !c.revoked);
        dialog.innerHTML = `<form id="clicker-settings"><div class="section-heading"><h2 id="clicker-dialog-title">${escape(clicker.name)}</h2><button type="button" id="close-clicker" class="text">Close</button></div><p role="status" aria-live="polite" class="account-message"></p><fieldset class="channel-choices"><legend>Send clicks to</legend>${channels.length ? channels.map(c => `<label><input type="checkbox" name="channel" value="${c.id}" ${clicker.channels.includes(c.id) ? "checked" : ""}><span>${escape(c.name)}${c.pro_until > Date.now() ? "" : '<small>Needs a plan</small>'}</span></label>`).join("") : '<p>No channels yet. Close this dialog and choose Add Channel.</p>'}</fieldset><div class="clicker-dialog-actions"><button type="submit">Save</button><button type="button" class="text" id="unlink-clicker">Unlink</button></div></form>`;
        dialog.showModal();
        root.querySelector<HTMLButtonElement>("#close-clicker")!.onclick = () => dialog.close();
        const form = dialog.querySelector<HTMLFormElement>("form")!;
        form.onsubmit = e => {
          e.preventDefault();
          const channels = new FormData(form).getAll("channel");
          void run(async () => {
            await api("/api/clickers/routes", { routes: [{ id: clicker.id, channels }] });
            dialog.close(); await refresh(); message("Channels saved.");
            root.querySelector<HTMLButtonElement>(`[data-clicker="${clicker.id}"] button`)?.focus();
          });
        };
        root.querySelector<HTMLButtonElement>("#unlink-clicker")!.onclick = () => {
          if (!confirm("Unlink this clicker? It will stop sending cloud clicks. To link it again, reset its account connection.")) return;
          void run(async () => { await api(`/api/clickers/${clicker.id}/unlink`, {}); dialog.close(); await refresh(); message("Clicker unlinked."); root.querySelector<HTMLButtonElement>("#add-clicker")!.focus(); });
        };
      };
      for (const button of root.querySelectorAll<HTMLButtonElement>("[data-clicker-link]")) button.onclick = () => void run(async () => {
        const result = await api<{ url: string }>(`/api/rooms/${button.dataset.clickerLink}/clicker-link`, {});
        try { await navigator.clipboard.writeText(result.url); message("Clicker link copied. It links one clicker and expires in 10 minutes."); }
        catch {
          const field = root.querySelector<HTMLInputElement>(`#invite-${button.dataset.clickerLink}`)!;
          field.parentElement!.hidden = false; field.value = result.url; field.focus(); field.select();
          message("Copy the selected link. It links one clicker and expires in 10 minutes.");
        }
      });
      on("add-channel", async () => { root.querySelector<HTMLDialogElement>("#channel-create-dialog")!.showModal(); });
      root.querySelector<HTMLButtonElement>("#close-create-channel")!.onclick = () => root.querySelector<HTMLDialogElement>("#channel-create-dialog")!.close();
      for (const row of root.querySelectorAll<HTMLTableRowElement>("[data-channel]")) row.onclick = event => {
        if ((event.target as Element).closest("[data-clicker-link], a")) return;
        const channel = current!.rooms.find(c => c.id === row.dataset.channel)!;
        const dialog = root.querySelector<HTMLDialogElement>("#channel-dialog")!;
        dialog.innerHTML = `<form id="channel-settings"><div class="section-heading"><h2 id="channel-dialog-title">Channel</h2><button type="button" class="text" id="close-channel">Close</button></div><p class="account-message" role="status" aria-live="polite"></p><p><a href="/manual/${channel.id}">Open Manual view</a></p><p class="hint">Share the Manual view’s address for public forward/back cues. No login needed; anyone with the link can view cues while this channel’s plan is active.</p><label>Channel name<input name="name" value="${escape(channel.name)}" maxlength="80" required></label><div class="clicker-dialog-actions"><button type="submit">Save</button><button type="button" class="text" id="revoke-channel">Revoke</button></div>${channel.pro_until > Date.now() ? `<p class="hint">${escape(channel.status)} until ${escape(new Date(channel.pro_until).toLocaleDateString())}</p>` : `<p class="hint">Renews automatically. Cancel before renewal in billing. Payment method required.</p><button type="button" data-subscribe="${channel.id}" data-interval="month" ${config.billing ? "" : "disabled"}>${channel.trial_used ? "Subscribe" : "Start free month"} · $3/month</button><button type="button" class="secondary" data-subscribe="${channel.id}" data-interval="year" ${config.billing ? "" : "disabled"}>${channel.trial_used ? "Subscribe" : "Start free month"} · $25/year</button>`}</form>`;
        dialog.showModal();
        root.querySelector<HTMLButtonElement>("#close-channel")!.onclick = () => dialog.close();
        const form = dialog.querySelector<HTMLFormElement>("form")!;
        form.onsubmit = e => { e.preventDefault(); const name = new FormData(form).get("name"); void run(async () => { await api(`/api/rooms/${channel.id}/rename`, { name }); dialog.close(); await refresh(); message("Channel renamed."); root.querySelector<HTMLButtonElement>(`[data-channel="${channel.id}"] .channel-name`)?.focus(); }); };
        root.querySelector<HTMLButtonElement>("#revoke-channel")!.onclick = () => {
          if (!confirm("Revoke this channel? Connected devices will disconnect immediately and its subscription will stop renewing. This cannot be undone.")) return;
          void run(async () => { await api(`/api/rooms/${channel.id}/revoke`, {}); dialog.close(); await refresh(); message("Channel revoked."); });
        };
        for (const button of dialog.querySelectorAll<HTMLButtonElement>("[data-subscribe]")) button.onclick = () => void run(() => subscribe(button.dataset.interval!, channel.id));
      };
      const roomForm = root.querySelector<HTMLFormElement>("#new-room");
      if (roomForm) roomForm.onsubmit = e => { e.preventDefault(); const name = new FormData(roomForm).get("room-name"); void run(async () => {
        if (channelCodes) { message("Save the displayed codes and choose Done before creating another channel."); return; }
        const credentials = await api<{ v: number; room: string; txToken: string; clientToken: string }>("/v1/rooms", { name });
        channelCodes = { client: JSON.stringify(credentials), tx: JSON.stringify({ v: 1, room: credentials.room, token: credentials.txToken }) };
        root.querySelector<HTMLDialogElement>("#channel-create-dialog")!.close();
        await refresh();
        root.querySelector<HTMLElement>("#room-codes")!.scrollIntoView({ block: "nearest", behavior: "auto" });
      }); };
      for (const button of root.querySelectorAll<HTMLButtonElement>("[data-copy]")) button.onclick = () => void run(async () => {
        const field = root.querySelector<HTMLTextAreaElement>(`#${button.dataset.copy}`)!;
        try { await navigator.clipboard.writeText(field.value); message("Code copied."); }
        catch { field.select(); message("Select and copy the code below."); }
      });
      on("hide-codes", async () => { channelCodes = null; for (const field of root.querySelectorAll<HTMLTextAreaElement>("#room-codes textarea")) field.value = ""; root.querySelector<HTMLElement>("#room-codes")!.hidden = true; });
    } else {
      const form = root.querySelector<HTMLFormElement>("#auth-form")!;
      form.onsubmit = e => {
        e.preventDefault();
        const email = String(new FormData(form).get("email") ?? "");
        void run(async () => {
          await api("/api/auth/sign-in/magic-link", { email, callbackURL: "/#account", errorCallbackURL: "/#account" });
          message("Check your inbox for a sign-in link. It expires in 10 minutes.");
        });
      };

    }
  }
  function renderAccountMenu() {
    const avatar = document.querySelector<HTMLButtonElement>("#account-avatar")!;
    const menu = document.querySelector<HTMLElement>("#account-menu")!;
    const external = !capabilities.accountInApp || !["app.uberclicker.com", "localhost", "127.0.0.1"].includes(location.hostname);
    avatar.textContent = Array.from(current?.user.name.trim() ?? "")[0]?.toLocaleUpperCase() || "U";
    menu.innerHTML = current ? `<p class="account-menu-name">${escape(current.user.name)}</p><button type="button" id="edit-display-name">Edit display name</button><button type="button" id="open-billing" ${current.billing.hasCustomer && config.billing ? "" : "disabled"}>Billing</button><button type="button" id="account-signout">Sign out</button><p role="status" aria-live="polite" class="hint"></p>` : `<a href="${external ? "https://app.uberclicker.com/#account" : "#account"}" ${external ? 'target="_blank" rel="noopener"' : ""}>${external ? "Open account" : "Sign in"}</a>`;
    menu.onbeforetoggle = () => { const bounds = avatar.getBoundingClientRect(); menu.style.top = `${bounds.bottom + 8}px`; menu.style.right = `${Math.max(16, innerWidth - bounds.right)}px`; };
    menu.querySelector("a")?.addEventListener("click", () => menu.hidePopover());
    if (!current) return;
    menu.querySelector<HTMLButtonElement>("#edit-display-name")!.onclick = () => {
      menu.hidePopover();
      const dialog = document.querySelector<HTMLDialogElement>("#profile-dialog")!;
      dialog.innerHTML = `<form><div class="section-heading"><h2 id="profile-title">Display name</h2><button type="button" class="text" id="close-profile">Close</button></div><label>Your name<input name="name" value="${escape(current!.user.name)}" maxlength="80" required autocomplete="name" autofocus></label><p role="status" aria-live="polite" class="account-message"></p><button type="submit">Save</button></form>`;
      dialog.showModal();
      dialog.querySelector<HTMLButtonElement>("#close-profile")!.onclick = () => dialog.close();
      const form = dialog.querySelector<HTMLFormElement>("form")!;
      form.onsubmit = e => {
        e.preventDefault(); const name = String(new FormData(form).get("name"));
        const button = form.querySelector<HTMLButtonElement>('[type="submit"]')!; button.disabled = true;
        void (async () => { try { await api("/api/auth/update-user", { name }); await refresh(); dialog.close(); avatar.focus(); } catch (e) { dialog.querySelector<HTMLElement>("[role=status]")!.textContent = e instanceof Error ? e.message : "Could not save your name."; } finally { button.disabled = false; } })();
      };
    };
    const signout = menu.querySelector<HTMLButtonElement>("#account-signout")!;
    signout.onclick = () => { signout.disabled = true; void (async () => { try { await api("/api/auth/sign-out", {}); menu.hidePopover(); current = null; channelCodes = null; render(); } catch (e) { menu.querySelector<HTMLElement>("[role=status]")!.textContent = e instanceof Error ? e.message : "Could not sign out."; } finally { signout.disabled = false; } })(); };
    const billing = menu.querySelector<HTMLButtonElement>("#open-billing")!;
    billing.onclick = () => {
      const popup = window.open("about:blank", "_blank");
      if (!popup) { menu.querySelector<HTMLElement>("[role=status]")!.textContent = "Allow pop-ups to open Stripe billing."; return; }
      popup.opener = null; billing.disabled = true;
      void (async () => { try { const result = await api<{ url: string }>("/api/billing/portal", {}); popup.location.replace(result.url); menu.hidePopover(); } catch (e) { popup.close(); menu.querySelector<HTMLElement>("[role=status]")!.textContent = e instanceof Error ? e.message : "Could not open billing."; } finally { billing.disabled = false; } })();
    };
  }
  async function subscribe(interval: string, room: string) { const result = await api<{ url: string }>("/api/billing/checkout", { interval, room }); location.assign(result.url); }
  function login() {
    return `<form id="auth-form" class="login-panel"><label for="signin-email" class="sr-only">Email address</label><input id="signin-email" name="email" type="email" placeholder="Email address" autocomplete="email" autocapitalize="none" spellcheck="false" maxlength="254" required><button type="submit" aria-label="Email me a sign-in link" ${config.email ? "" : "disabled"}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 12h14m-6-6 6 6-6 6"/></svg></button></form>`;
  }
  function seen(at?: number) {
    if (!at) return "Not yet";
    const minutes = Math.max(0, Math.floor((Date.now() - at) / 60000));
    return minutes < 1 ? "Just now" : minutes < 60 ? `${minutes}m ago` : minutes < 1440 ? `${Math.floor(minutes / 60)}h ago` : new Date(at).toLocaleDateString();
  }
  function dashboard(a: Account) {
    const devices = [...(a.devices ?? [])].sort((a, b) => (b.last_seen ?? 0) - (a.last_seen ?? 0));
    return `<section class="clickers-section" aria-labelledby="clickers-title"><div class="section-heading"><h1 id="clickers-title">Cue Remote</h1><button type="button" id="add-clicker" aria-expanded="false" aria-controls="clicker-pairing">Add</button></div>
    <table class="clickers-table"><caption class="sr-only">Cue Remote devices linked to your account</caption><thead><tr><th scope="col">Clicker</th><th scope="col">Channels</th><th scope="col" aria-sort="descending">Last seen</th></tr></thead><tbody>${devices.length ? devices.map(d => `<tr data-clicker="${d.id}"><th scope="row"><button type="button" class="clicker-name">${escape(d.name)}</button></th><td>${d.channels.map(id => escape(a.rooms.find(r => r.id === id)?.name ?? "Unknown channel")).join(", ") || "None"}</td><td>${d.last_seen ? `<time datetime="${new Date(d.last_seen).toISOString()}" title="${escape(new Date(d.last_seen).toLocaleString())}">${escape(seen(d.last_seen))}</time>` : "Not yet"}</td></tr>`).join("") : '<tr><td colspan="3">No clickers linked. Choose Add to link your first clicker.</td></tr>'}</tbody></table>
    <div id="clicker-pairing" hidden><h3>Add a clicker</h3><p>Enter the pairing code displayed on your M5 or in the mobile app.</p><ul class="pairing-help"><li><strong>M5:</strong> connect to Wi-Fi and enable Cloud actions. The code appears on its screen.</li><li><strong>Mobile app:</strong> open Cue. The pairing code appears automatically.</li></ul><form id="link-clicker"><label>Pairing code<input name="code" placeholder="ABCD EFGH" autocomplete="one-time-code" spellcheck="false" autocapitalize="characters" maxlength="12" aria-describedby="pairing-hint" required></label><p id="pairing-hint" class="hint">Codes expire after 10 minutes. Linking adds the clicker to your account; tap the clicker to choose its channels.</p><div class="pairing-actions"><button type="submit">Link clicker</button><button type="button" class="secondary" id="cancel-link">Cancel</button></div></form></div></section>
    <dialog id="clicker-dialog" aria-labelledby="clicker-dialog-title"></dialog>
    <section class="channels-section" aria-labelledby="channels-title"><div class="section-heading"><h2 id="channels-title">Channels</h2><button type="button" id="add-channel">Add Channel</button></div>
    <table class="channels-table"><caption class="sr-only">Your Cloud Channels</caption><thead><tr><th scope="col">Channel</th><th scope="col">Plan</th><th scope="col"><span class="sr-only">Channel links</span></th></tr></thead><tbody>${a.rooms.filter(r => !r.revoked).map(r => `<tr data-channel="${r.id}"><th scope="row"><button type="button" class="channel-name">${escape(r.name)}</button></th><td>${r.pro_until > Date.now() ? escape(r.status) : "Needs a plan"}</td><td><a class="manual-link" href="/manual/${r.id}" aria-label="Open Manual view for ${escape(r.name)}">Manual</a><button type="button" class="text" data-clicker-link="${r.id}" aria-label="Copy clicker link for ${escape(r.name)}">Copy link</button><label hidden>Clicker link<input id="invite-${r.id}" readonly></label></td></tr>`).join("") || '<tr><td colspan="3">No channels yet. Choose Add Channel to create one.</td></tr>'}</tbody></table>
    <dialog id="channel-create-dialog" class="account-dialog" aria-labelledby="channel-create-title"><form id="new-room"><div class="section-heading"><h2 id="channel-create-title">Add Channel</h2><button type="button" id="close-create-channel" class="text">Close</button></div><p role="status" class="account-message" aria-live="polite"></p><label>Channel name<input name="room-name" placeholder="Main stage" maxlength="80" required autofocus></label><p class="hint">$3 AUD/month or $25 AUD/year per channel. First month free.</p><button type="submit">Create channel</button></form></dialog>
    <dialog id="channel-dialog" class="account-dialog" aria-labelledby="channel-dialog-title"></dialog>
    <div id="room-codes" class="room-codes" ${channelCodes ? "" : "hidden"}><h3>Connect a Client Companion</h3><p>Save these codes now. They are shown only once. Keep them private; lost codes require a new channel.</p><label>Client Companion code<textarea id="companion-code" readonly rows="2" spellcheck="false">${escape(channelCodes?.client ?? "")}</textarea></label><button type="button" data-copy="companion-code" class="secondary">Copy listener code</button><p>Paste this in Listen on each Client Companion. Up to eight can listen.</p><details><summary>Manual clicker connection</summary><p>For clickers that cannot display an account linking code.</p><label>Clicker code<textarea id="tx-code" readonly rows="2" spellcheck="false">${escape(channelCodes?.tx ?? "")}</textarea></label><button type="button" data-copy="tx-code" class="secondary">Copy clicker code</button></details><button id="hide-codes" class="text">Done — hide codes</button></div>
    </section>`;
  }

  let refreshing = false;
  async function refreshVisible() {
    const account = current;
    const safeToRefresh = () => !!account && current === account && !root.hidden && document.visibilityState === "visible" && !document.querySelector("dialog[open], [popover]:popover-open") && !!root.querySelector<HTMLElement>("#clicker-pairing")?.hidden && !channelCodes && !root.querySelector("input:focus, button:disabled");
    if (refreshing || !safeToRefresh()) return;
    refreshing = true;
    try { await refresh(safeToRefresh); } catch { /* Keep the last usable table during a temporary outage. */ } finally { refreshing = false; }
  }
  window.addEventListener("uberclicker:navigate", () => void refreshVisible());
  document.addEventListener("visibilitychange", () => void refreshVisible());
  setInterval(() => void refreshVisible(), 30000);
  root.innerHTML = '<main class="account-page"><p role="status">Loading your account…</p></main>';
  if (!capabilities.accountInApp || !["app.uberclicker.com", "localhost", "127.0.0.1"].includes(location.hostname)) { render(); return; }
  void (async () => {
    try {
      config = await api<Config>("/api/config");
      if (new URL(location.href).searchParams.get("checkout") === "success") {
        await api("/api/billing/sync", {}); history.replaceState({}, "", "/#account");
      }
      await refresh();
      if (new URL(location.href).searchParams.has("error")) message("That sign-in link could not be used. Request a new one.");
    } catch { render(); message("Sign-in is temporarily unavailable. Please reload to try again."); }
  })();
}
