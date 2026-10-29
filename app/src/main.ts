import "./style.css";
import {
  DeviceSession,
  ReceiverTransport,
  keepPairs,
  removeReceiver,
  randomHex,
  type Config,
  type Action,
  type Secrets,
} from "./protocol";
import {
  BleTransport,
  NativeLanTransport,
  bleSupported,
  credentials,
  native,
} from "./transport";
import fixture from "../../contracts/fixtures/config-multi-rx.json";

const root = document.querySelector<HTMLDivElement>("#app")!;
let device: DeviceSession | undefined,
  draft: Config | undefined,
  busy = false,
  message = "",
  isError = false;
let receiver: DeviceSession | undefined;
let receiverId = "";
function operationJournal(id: string) {
  const key = `apply:${device!.status!.deviceId}:${id}`;
  return {
    load: () => credentials.get(key),
    save: (transaction: string) => credentials.set(key, transaction),
    clear: () => credentials.remove(key),
  };
}
function selected() {
  return receiver || device!;
}
let pairing:
  | {
      firstId: string;
      secondId: string;
      pairId: string;
      key: string;
      firstRole: "tx" | "rx";
      firstApplied: boolean;
    }
  | undefined;
let wifiEdit = { op: "keep", value: "" };
const preview = new URLSearchParams(location.search).has("preview");
const escape = (s: unknown) =>
  String(s ?? "").replace(
    /[&<>"']/g,
    (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        c
      ]!,
  );
function input(
  label: string,
  id: string,
  value: unknown,
  type = "text",
  extra = "",
) {
  return `<label>${label}<input id="${id}" type="${type}" value="${escape(value)}" ${extra}></label>`;
}
function select(label: string, id: string, value: unknown, options: string[]) {
  return `<label>${label}<select id="${id}">${options.map((o) => `<option ${o === value ? "selected" : ""} value="${o}">${o.replaceAll("_", " ")}</option>`).join("")}</select></label>`;
}
function notify(s: string, error = false) {
  message = s;
  isError = error;
}
function lost() {
  receiver?.lost();
  receiver = undefined;
  receiverId = "";
  device?.lost();
  device = undefined;
  draft = undefined;
  wifiEdit = { op: "keep", value: "" };
  notify(
    "Disconnected. Staged changes were discarded. Reopen setup on your clicker, reconnect, and reread settings.",
    true,
  );
  render();
}
async function run(fn: () => Promise<void>) {
  if (busy) return;
  busy = true;
  for (const b of root.querySelectorAll<HTMLButtonElement>("button"))
    if (b.id !== "disconnect") b.disabled = true;
  for (const field of root.querySelectorAll<
    HTMLInputElement | HTMLSelectElement
  >("input,select"))
    field.disabled = true;
  if (receiver) {
    const badge = document.querySelector(".state");
    if (badge) badge.textContent = "Waiting for receiver";
    const status = document.getElementById("message");
    if (status)
      status.textContent = `Waiting for receiver ${receiverId} through Tx. LoRa configuration can take minutes; cancel does not imply a committed change was undone.`;
  }
  try {
    await fn();
  } catch (e) {
    const errors: Record<string, string> = {
      RECOVERY_REQUIRED:
        "Stored settings need physical recovery. Use the clicker factory-reset menu; no settings were erased automatically.",
      TRANSACTION_CONFLICT:
        "This Apply identifier was reused with different content. Resolve the previous result before a new edit.",
      SETUP_REQUIRED:
        "Open App setup on the clicker, then choose Authorize again.",
      UNAUTHORIZED:
        "Authorize this app while the physical setup window is open.",
      SESSION_EXPIRED: receiver
        ? "Receiver edit expired. Read it again through Tx and start a new edit."
        : "The edit expired. Reopen setup, authorize, and start a new edit.",
      STALE_REVISION:
        "Settings changed. Cancel and reread before editing again.",
      INVALID:
        "Some settings are invalid. Check names, endpoints, mappings, and secret actions.",
      PERSIST_FAILED:
        "The device could not save settings. Reconnect and reread before retrying.",
      BUSY: "An edit is already open. Cancel it or disconnect and start again.",
      RATE_LIMITED: "Too many failed attempts. Reopen setup physically.",
      REMOTE_UNAVAILABLE:
        "Receiver did not respond. Check its power/radio range, then read again. No change is confirmed.",
      REMOTE_TIMEOUT:
        "Receiver outcome is unknown. Reconnect through Tx and reread its revision before retrying.",
      REMOTE_DENIED:
        "This receiver or operation is not authorized for this transmitter.",
    };
    const reason = e instanceof Error ? e.message : "Action failed";
    notify(errors[reason] ?? reason, true);
  } finally {
    busy = false;
    render();
  }
}
function actionForm(name: string, a: Action) {
  return `<fieldset><legend>${name === "previous" ? "Previous" : "Next"}</legend><div class="fields">${input("Keyboard usage", `${name}-key`, a.key, "number", 'min="0" max="101"')}${input("Modifier bits", `${name}-modifiers`, a.modifiers, "number", 'min="0" max="255"')}${select("MIDI message", `${name}-type`, a.midi.type, ["note", "cc", "program"])}${input("MIDI channel", `${name}-channel`, a.midi.channel, "number", 'min="1" max="16"')}${input("Note / controller / program", `${name}-number`, a.midi.number, "number", 'min="0" max="127"')}${input("MIDI value", `${name}-value`, a.midi.value, "number", 'min="0" max="127"')}${input("OSC address", `${name}-osc`, a.osc, "text", 'maxlength="64"')}${input("UDP / TCP text", `${name}-payload`, a.payload, "text", 'maxlength="128"')}</div></fieldset>`;
}
function render() {
  const s = receiver?.status || device?.status,
    c = draft || receiver?.config || device?.config;
  const updates = !receiver && c && !draft && s?.setup && s.capabilities.ota_session ? `<section><h2>Firmware updates</h2><p>Open the update screen on this clicker. Checking and installation use its buttons; the app cannot start a firmware write.</p><button id="updates">Open update screen</button></section>` : "";
  root.innerHTML = `<header><a class="brand" href="./">UberClicker<span class="brand-mark" aria-hidden="true">◀ ▶</span></a><span>Nearby setup</span></header><main><div class="intro"><h1>${receiver ? escape(device?.config?.peers.find((p) => p.deviceId === receiverId)?.name || "Receiver settings") : draft ? "Make it yours." : s ? "Your clicker." : "A clicker. Your way."}</h1><p>${draft ? "Clicks are paused while you edit. Apply saves every change together." : "Set up your buttons, pair a receiver, and get back to presenting."}</p></div>
 ${preview ? '<p class="notice">UI preview · fixture data only · device actions disabled</p>' : ""}
 <p id="message" class="notice ${isError ? "error" : ""}" role="status" aria-live="polite">${escape(message || (receiver ? "Receiver settings arrive through Tx. No physical Rx setup is needed." : "On your transmitter, open Pair new RX to start physical BLE setup."))}</p>
 ${receiver ? `<aside class="notice">Configuring ${escape(receiverId)} through transmitter ${escape(device?.status?.deviceId)}. Receiver changes apply only to this target.<button id="backToTx" class="text-button">Back to transmitter</button></aside>` : ""}
 ${pairing ? `<aside class="notice">Pairing incomplete. First device ${escape(pairing.firstId)} ${pairing.firstApplied ? "is configured. Connect " + escape(pairing.secondId) + " to complete the pair." : "has an uncertain Apply outcome. Reconnect and reread the first device before continuing."} Keep this app open; closing it discards the temporary key.<button id="discardPair" class="text-button">Discard temporary pair key</button></aside>` : ""}
 ${
   !s
     ? `<section class="connect"><div class="device-drawing" aria-hidden="true"><div>Presenter<strong>Previous　Next</strong></div><i></i><i></i></div><div><h2>Find your clicker</h2><p>Keep it nearby. Select it in the Bluetooth chooser, then enter the code shown on its screen when your phone asks to pair.</p><button id="connect" ${!bleSupported() || preview ? "disabled" : ""}>Find nearby clicker</button>${!bleSupported() ? '<p class="hint">This browser cannot use Bluetooth setup. Use the native iOS app, or a browser with Web Bluetooth on Android or desktop. The device does not currently provide a Wi-Fi setup page.</p>' : ""}</div></section>${native ? "<details><summary>Protected LAN connection</summary><p>For firmware advertising LAN configuration with a trusted HTTPS certificate. Development M5 firmware provides BLE setup only.</p>" + input("HTTPS device address", "lan", "https://") + '<button id="lanConnect">Connect by address</button></details>' : ""}`
     : `<section class="identity"><div><h2>${escape(c?.profiles[c.activeProfile]?.name || "UberClicker")}</h2><p>${escape(s.deviceId)} · ${escape(s.boardId)}</p><p>Firmware ${escape(s.firmwareVersion)} · Settings v${s.schemaVersion} · Revision ${c?.revision ?? s.revision}</p></div><span class="state">${busy && receiver ? "Waiting for receiver" : draft ? "Clicks paused" : receiver ? "Managed through Tx" : s.setup ? "Setup window open" : "Reopen physical setup"}</span></section>
 ${s.recoveryRequired ? "<section><h2>Physical recovery required</h2><p>Stored settings use an unsupported or damaged format. Configuration is locked and outputs stay paused. Reboot and reread first. If the saved format remains unsupported, install compatible firmware or use the physical factory-reset menu. The app will not silently erase it.</p></section>" : !c ? `<section><h2>${s.claimed ? "Authorize this app" : "Claim this clicker"}</h2><p>${s.claimed ? "Use the credentials saved in this native app. If unavailable, revoke the owner from an authorized app or reset the device physically." : "Enter the code displayed on the clicker. Bluetooth pairing and device ownership are separate checks."}</p>${!s.claimed ? input("Six-digit device code", "code", "", "text", 'inputmode="numeric" pattern="[0-9]{6}" maxlength="6" autocomplete="off"') + '<button id="claim">Claim clicker</button>' : '<button id="authorize">Use saved authorization</button>'}</section>` : ""}
 ${!draft && device?.config?.role === "tx" ? receiverPanel(device.config) : !draft && !receiver && device?.config?.role === "rx" ? `<section><h2>Controller authority</h2>${device.config.peers.map((p, i) => `<p>${escape(p.deviceId)} · ${p.adminAuthorized ? "Remote administration approved" : "Cue pairing only"}</p>${!p.adminAuthorized ? `<button data-grant="${i}">Approve remote administration</button>` : ""}`).join("")}</section>` : ""}
 ${c && !draft ? `<section><h2>${receiver?.pendingApply ? "Check the previous Apply" : "Your settings are ready"}</h2>${receiver?.pendingApply ? "<p>These are the last read settings. Resolve the previous Apply and reread this receiver before changing it.</p>" : ""}<div class="action-strip"><div><span>Previous</span><strong>${escape(c.profiles[c.activeProfile].previous.payload || "Previous")}</strong></div><div><span>Next</span><strong>${escape(c.profiles[c.activeProfile].next.payload || "Next")}</strong></div></div><p>${escape(c.role === "tx" ? "Transmitter" : "Receiver")} · ${escape(c.route === "lora" ? "LoRa pair" : "Direct output")} · ${escape(c.profiles[c.activeProfile].output.replaceAll("_", " "))}</p>${receiver?.pendingApply ? '<button id="resolveApply">Check previous Apply result</button><button id="discardApply" class="secondary">Discard unresolved lookup</button>' : '<button id="edit">Edit settings</button>'}</section>${!receiver ? `<details><summary>Add a receiver</summary><p>Open setup on both devices. Enter the other clicker’s 12-character device ID. Each device must be physically authorized. The pair becomes usable only after both Apply operations. Pairing grants the transmitter remote administration of the receiver’s settings.</p>${input("Receiver name", "peerName", "", "text", 'maxlength="24"')}${input("Other device ID", "peer", "", "text", 'pattern="[0-9a-f]{12}" maxlength="12"')}<button id="pairStart">Configure first device</button>${pairing ? '<button id="pairFinish">Complete on this device</button>' : ""}</details><details><summary>Remove app authorization</summary><p>Revokes all saved owner tokens and BLE bonds on this clicker. New physical claim is required.</p><button id="revoke" class="secondary">Revoke owner</button></details>` : ""}` : ""}
 ${draft ? editor(draft) : ""}${updates}<div class="footer-actions">${c && s.claimed ? '<button id="authorize" class="text-button">Authorize again</button>' : ""}<button id="refresh" class="text-button">Reread device</button><button id="disconnect" class="text-button">Disconnect</button></div>`
 }
 <footer>Local setup. No account needed.<br>Bluetooth, display, and radio behavior still require hardware acceptance testing.<br><a href="./third-party-notices.txt">Third-party notices</a></footer></main>`;
  bind();
  if (receiver)
    for (const id of ["role", "route", "radio"]) {
      const el = document.getElementById(id) as HTMLSelectElement | null;
      if (el) el.disabled = true;
    }
  for (const b of root.querySelectorAll<HTMLButtonElement>("button"))
    if (preview || (busy && b.id !== "disconnect")) b.disabled = true;
  if (busy)
    for (const field of root.querySelectorAll<
      HTMLInputElement | HTMLSelectElement
    >("input,select"))
      field.disabled = true;
}
function receiverPanel(c: Config) {
  return `<section><h2>Receivers</h2><p>Choose a receiver to configure over LoRa. You do not need to open setup on it again. The cue receiver is selected separately.</p>${!device?.status?.capabilities.remote_config ? '<p class="notice">This firmware does not provide remote configuration.</p>' : ""}${c.peers.length ? c.peers.map((p, i) => `<div class="receiver-row"><div><strong>${escape(p.name)}</strong><p>${escape(p.deviceId)}${i === c.activePeer ? " · Cue receiver" : ""}</p></div><div><button data-receiver="${p.deviceId}" ${!device?.status?.capabilities.remote_config || !p.adminAuthorized ? "disabled" : ""}>Configure</button><button class="secondary" data-cue="${i}" ${i === c.activePeer ? "disabled" : ""}>Use for cues</button>${!p.adminAuthorized ? `<button class="secondary" data-grant="${i}">Approve remote administration</button>` : ""}<button class="text-button" data-remove="${p.deviceId}">Remove</button></div></div>`).join("") : "<p>No paired receivers. Add one below with physical setup on both devices.</p>"}</section>`;
}
function editor(c: Config) {
  const p = c.profiles[c.activeProfile];
  return `<form id="editor"><section><h2>Device & route</h2>${receiver ? '<p class="hint">Controller and radio settings stay unchanged during remote edits.</p>' : ""}<div class="fields">${select("Role", "role", c.role, ["tx", "rx"])}${select("Transmitter route", "route", c.route, ["lora", "direct"])}${select("Radio preset", "radio", c.radio, ["balanced", "long_range"])}</div></section><section><h2>Profiles</h2><div class="profile-tabs">${c.profiles.map((p, i) => `<button type="button" data-profile="${i}" class="${i === c.activeProfile ? "selected" : "secondary"}">${escape(p.name)}</button>`).join("")}<button type="button" id="addProfile" class="text-button" ${c.profiles.length >= 4 ? "disabled" : ""}>Add profile</button></div><div class="fields">${input("Profile name", "profileName", p.name, "text", 'required maxlength="24"')}${select("Output", "output", p.output, ["ble_keyboard", "ble_midi", "osc_udp", "udp", "tcp"])}${input("Destination host / IPv4", "host", p.host, "text", 'maxlength="63"')}${input("Destination port", "port", p.port, "number", 'min="1" max="65535" required')}</div><p class="hint">Keyboard usages 80 / 79 are Left / Right. MIDI notes send release; OSC sends one int32 value. TCP appends a newline. UDP send completion does not prove delivery.</p>${actionForm("previous", p.previous)}${actionForm("next", p.next)}${c.profiles.length > 1 ? '<button type="button" id="removeProfile" class="text-button">Remove active profile</button>' : ""}</section><section><h2>Wi-Fi</h2><p>The management link stays available if joining Wi-Fi fails. Stored passwords are never read back.</p><div class="fields">${input("Network name", "ssid", c.wifi.ssid, "text", 'maxlength="32"')}${select("Password action", "wifiOp", wifiEdit.op, ["keep", "replace", "clear"])}${input("New password", "wifiPassword", wifiEdit.value, "password", 'minlength="8" maxlength="63" autocomplete="new-password"')}</div></section><section class="apply-bar"><p>Save all changes to this clicker. A disconnected or expired edit must be started again.</p><button type="submit">Apply changes</button><button id="cancel" type="button" class="secondary">Cancel</button></section></form>`;
}
function value(id: string) {
  return (document.getElementById(id) as HTMLInputElement).value;
}
function capture() {
  if (!draft) return;
  draft.role = value("role") as Config["role"];
  draft.route = value("route") as Config["route"];
  draft.radio = value("radio") as Config["radio"];
  draft.wifi.ssid = value("ssid");
  wifiEdit = { op: value("wifiOp"), value: value("wifiPassword") };
  const p = draft.profiles[draft.activeProfile];
  p.name = value("profileName");
  p.output = value("output") as typeof p.output;
  p.host = value("host");
  p.port = Number(value("port"));
  for (const name of ["previous", "next"] as const) {
    const a = p[name];
    a.key = Number(value(`${name}-key`));
    a.modifiers = Number(value(`${name}-modifiers`));
    a.midi.type = value(`${name}-type`) as Action["midi"]["type"];
    for (const field of ["channel", "number", "value"] as const)
      a.midi[field] = Number(value(`${name}-${field}`));
    a.osc = value(`${name}-osc`);
    a.payload = value(`${name}-payload`);
  }
}
function on(id: string, fn: () => Promise<void>) {
  document.getElementById(id)?.addEventListener("click", () => void run(fn));
}
function verifyFirstPair() {
  if (
    pairing &&
    device?.status?.deviceId === pairing.firstId &&
    device.config?.peers.some(
      (p) => p.pairId === pairing!.pairId && p.deviceId === pairing!.secondId,
    )
  )
    pairing.firstApplied = true;
}
async function connected(
  transport: BleTransport | NativeLanTransport,
  lan = false,
) {
  device = new DeviceSession(transport);
  await device.refresh();
  if (lan && !device.status!.capabilities.lan_config)
    throw Error("Device does not advertise LAN configuration");
  notify(
    "Connected. Authorize this app while the physical setup window is open.",
  );
  const token = await credentials.get(device.status!.deviceId);
  if (token) {
    await device.authorize(token);
    verifyFirstPair();
    notify("Authorized. Your committed settings are ready.");
  }
}
function bind() {
  on("resolveApply", async () => {
    const result = await receiver!.resolveApply();
    notify(
      result.state === "applied"
        ? `Receiver Apply confirmed at revision ${result.revision}; its current settings were reread.`
        : "Apply outcome remains unknown. No Apply was retried. Read the receiver settings before deciding on a new change.",
      result.state !== "applied",
    );
  });
  on("discardApply", async () => {
    await receiver!.discardPending();
    notify(
      "Lookup discarded locally. This does not undo a committed Apply. Read current receiver settings before a new edit.",
    );
  });
  root.querySelectorAll<HTMLButtonElement>("[data-grant]").forEach((b) =>
    b.addEventListener(
      "click",
      () =>
        void run(async () => {
          const c = await device!.begin();
          c.peers[Number(b.dataset.grant)].adminAuthorized = true;
          await device!.apply(c, { wifi: { op: "keep" }, pairs: keepPairs(c) });
          notify(
            "Remote administration approved on this device. The other side also needs explicit physical authorization for a migrated pair.",
          );
        }),
    ),
  );

  root.querySelectorAll<HTMLButtonElement>("[data-receiver]").forEach((b) =>
    b.addEventListener(
      "click",
      () =>
        void run(async () => {
          if (draft) throw Error("Apply or cancel before switching receivers");
          receiverId = b.dataset.receiver!;
          receiver = new DeviceSession(
            new ReceiverTransport(device!.transport, receiverId),
            operationJournal(receiverId),
          );
          await receiver.restorePending();
          notify(
            `Contacting receiver ${receiverId} over LoRa. This can take minutes; no settings are cached as live.`,
          );
          render();
          try {
            await receiver.refresh();
            await receiver.read();
            notify(
              `Receiver ${receiverId} read directly through Tx. Revision ${receiver.config!.revision}.`,
            );
          } catch (e) {
            receiver = undefined;
            receiverId = "";
            throw e;
          }
        }),
    ),
  );
  root.querySelectorAll<HTMLButtonElement>("[data-cue]").forEach((b) =>
    b.addEventListener(
      "click",
      () =>
        void run(async () => {
          const c = await device!.begin();
          c.activePeer = Number(b.dataset.cue);
          await device!.apply(c, { wifi: { op: "keep" }, pairs: keepPairs(c) });
          notify("Cue receiver changed. The configuration target is separate.");
        }),
    ),
  );
  root.querySelectorAll<HTMLButtonElement>("[data-remove]").forEach((b) =>
    b.addEventListener(
      "click",
      () =>
        void run(async () => {
          const id = b.dataset.remove!;
          removeReceiver(structuredClone(device!.config!), id);
          const c = removeReceiver(await device!.begin(), id);
          await device!.apply(c, {
            wifi: { op: "keep" },
            pairs: [...keepPairs(c), { deviceId: id, op: "clear" }],
          });
          if (receiverId === id) {
            receiver = undefined;
            receiverId = "";
          }
          notify(
            "Receiver removed from Tx. Physically recover or re-pair it before assigning a different controller.",
          );
        }),
    ),
  );
  on("backToTx", async () => {
    try {
      if (receiver?.session) await receiver.cancel();
    } finally {
      receiver = undefined;
      receiverId = "";
      draft = undefined;
      wifiEdit = { op: "keep", value: "" };
    }
    notify("Transmitter selected.");
  });
  on("connect", async () => connected(await BleTransport.discover(lost)));
  on("lanConnect", async () => {
    await connected(new NativeLanTransport(value("lan")), true);
  });
  on("claim", async () => {
    const token = await device!.claim(value("code"));
    await credentials.set(device!.status!.deviceId, token);
    await device!.read();
    notify(
      native
        ? "Claimed. Authorization saved securely on this phone."
        : "Claimed for this tab. Keep it open; browser credentials are held only in memory.",
    );
  });
  on("authorize", async () => {
    await device!.refresh();
    const token = await credentials.get(device!.status!.deviceId);
    if (!token)
      throw Error(
        "No owner token on this phone. Use the app that claimed this clicker, or physically reset and claim again.",
      );
    await device!.authorize(token);
    notify("Authorized.");
  });
  on("updates", async () => {
    if(receiver) throw Error("Firmware updates require a direct local connection");
    await device!.openUpdateSession();
    await device!.refresh();
    notify("Update screen requested. On the clicker, press Check, review the version, then Install. Nothing was downloaded or written by this request.");
  });
  on("edit", async () => {
    draft = await selected().begin();
    wifiEdit = { op: "keep", value: "" };
    notify(
      receiver
        ? `Editing receiver ${receiverId}. Its output is paused until Apply, Cancel, or expiry.`
        : "Editing. Click output is paused.",
    );
  });
  on("refresh", async () => {
    if (draft) throw Error("Apply or cancel before rereading");
    await selected().refresh();
    await selected().read();
    verifyFirstPair();
    notify("Committed settings reread.");
  });
  document.getElementById("disconnect")?.addEventListener("click", () => {
    const transport = device?.transport;
    if (transport)
      void transport.disconnect().catch(() => {
        lost();
      });
  });
  on("cancel", async () => {
    try {
      await selected().cancel();
    } finally {
      draft = undefined;
      wifiEdit = { op: "keep", value: "" };
    }
    notify(
      receiver
        ? `Receiver ${receiverId} changes discarded. Tx stays paused until you leave its setup window.`
        : "Changes discarded. Leave setup on the clicker to resume clicks.",
    );
  });
  root.querySelector("#editor")?.addEventListener("submit", (e) => {
    e.preventDefault();
    if (!draft || busy) return;
    capture();
    const op = value("wifiOp");
    const secrets: Secrets = {
      wifi:
        op === "replace"
          ? { op: "replace", value: value("wifiPassword") }
          : { op: op as "keep" | "clear" },
      pairs: keepPairs(selected().config!),
    };
    draft!.wifi.configured =
      op === "keep" ? selected().config!.wifi.configured : op === "replace";
    const targetSession = selected(),
      configToApply = draft!;
    void run(async () => {
      try {
        await targetSession.apply(configToApply, secrets);
      } finally {
        if (!targetSession.session) {
          draft = undefined;
          wifiEdit = { op: "keep", value: "" };
        }
      }
      draft = undefined;
      notify(
        receiver
          ? `Receiver ${receiverId} applied and reread at revision ${receiver.config?.revision}. Leave setup on Tx to resume cues.`
          : "Changes applied. Leave setup on the clicker to resume clicks. Use BLE again if Wi-Fi joining fails.",
      );
    });
  });
  root.querySelectorAll<HTMLButtonElement>("[data-profile]").forEach((b) =>
    b.addEventListener("click", () => {
      capture();
      draft!.activeProfile = Number(b.dataset.profile);
      render();
    }),
  );
  on("addProfile", async () => {
    capture();
    const p = structuredClone(draft!.profiles[draft!.activeProfile]);
    p.name = `Profile ${draft!.profiles.length + 1}`;
    draft!.profiles.push(p);
    draft!.activeProfile = draft!.profiles.length - 1;
  });
  on("removeProfile", async () => {
    capture();
    draft!.profiles.splice(draft!.activeProfile, 1);
    draft!.activeProfile = 0;
  });
  on("revoke", async () => {
    await device!.transport.request("revoke");
    await credentials.remove(device!.status!.deviceId);
    device!.lost();
    await device!.refresh();
    notify("Owner revoked. Open setup and claim again.");
  });
  on("pairStart", async () => {
    if (pairing)
      throw Error("Complete or discard the existing temporary pair first");
    const target = value("peer"),
      id = device!.status!.deviceId;
    if (!/^[0-9a-f]{12}$/.test(target) || target === id)
      throw Error("Enter the other device’s 12-character ID");
    const name = value("peerName") || `Receiver ${target.slice(-4)}`;
    if (new TextEncoder().encode(name).length > 24)
      throw Error("Receiver names must fit 24 UTF-8 bytes");
    if (
      device!.config?.role !== "tx" ||
      device!.config.peers.length >= 8 ||
      device!.config.peers.some((p) => p.deviceId === target)
    )
      throw Error("Choose a transmitter with room for a new receiver");
    const c = await device!.begin();
    const p = {
      firstId: id,
      secondId: target,
      pairId: randomHex(8),
      key: randomHex(32),
      firstRole: c.role,
      firstApplied: false,
    };
    c.peers.push({
      deviceId: target,
      pairId: p.pairId,
      name,
      adminAuthorized: true,
    });
    pairing = p;
    await device!.apply(c, {
      wifi: { op: "keep" },
      pairs: c.peers.map((peer) =>
        peer.deviceId === target
          ? { deviceId: target, op: "replace", value: p.key }
          : { deviceId: peer.deviceId, op: "keep" },
      ),
    });
    p.firstApplied = true;
    notify(
      "First device configured. Disconnect, then physically authorize the second clicker. Pairing is incomplete.",
    );
  });
  on("pairFinish", async () => {
    const p = pairing;
    if (!p || !p.firstApplied || device!.status!.deviceId !== p.secondId)
      throw Error("Connect the second device listed above");
    const c = await device!.begin();
    if (
      c.peers.some(
        (peer) => peer.deviceId !== p.firstId || peer.pairId !== p.pairId,
      )
    ) {
      await device!.cancel();
      throw Error(
        "This receiver already has another controller. Use physical recovery before pairing.",
      );
    }
    c.peers = [
      {
        deviceId: p.firstId,
        pairId: p.pairId,
        name: "Controller",
        adminAuthorized: true,
      },
    ];
    c.activePeer = 0;
    c.role = "rx";
    c.route = "lora";
    await device!.apply(c, {
      wifi: { op: "keep" },
      pairs: [{ deviceId: p.firstId, op: "replace", value: p.key }],
    });
    p.key = "";
    pairing = undefined;
    notify(
      "Both configurations applied. Verify a physical click and receiver output.",
    );
  });
  on("discardPair", async () => {
    if (pairing) pairing.key = "";
    pairing = undefined;
    notify(
      "Temporary key discarded. Restart pairing on both clickers to replace the incomplete pair.",
    );
  });
}
setInterval(() => {
  if (
    !preview &&
    !busy &&
    receiver?.session &&
    receiver.leaseExpiresAt &&
    Date.now() >= receiver.leaseExpiresAt
  ) {
    receiver.session = undefined;
    receiver.leaseExpiresAt = undefined;
    draft = undefined;
    wifiEdit = { op: "keep", value: "" };
    notify(
      "Receiver edit idle deadline reached. No Apply was sent. Reopen setup on Tx if needed, reread the receiver, and start a new edit.",
      true,
    );
    render();
  }
}, 1000);
window.addEventListener("pagehide", () => {
  if (pairing) pairing.key = "";
  pairing = undefined;
});
if (preview) {
  device = new DeviceSession({
    request: async () => {
      throw Error("Preview only");
    },
    disconnect: async () => {},
  });
  device.config = structuredClone(fixture) as Config;
  device.status = {
    deviceId: "aabbccddeeff",
    boardId: "thinknode_m5",
    firmwareVersion: "0.1.0-preview",
    schemaVersion: 2,
    protocolVersion: 2,
    revision: 0,
    setup: true,
    editing: false,
    claimed: true,
    capabilities: {
      remote_config: true,
      ble_config: true,
      lan_config: false,
      setup_hotspot: false,
      outputs: ["ble_keyboard", "ble_midi", "osc_udp", "udp", "tcp"],
    },
  };
  const view = new URLSearchParams(location.search).get("preview");
  if (view === "receiver" || view === "receipt") {
    receiverId = device.config.peers[0].deviceId;
    receiver = new DeviceSession({
      request: async () => {
        throw Error("Preview only");
      },
      disconnect: async () => {},
    });
    receiver.status = { ...device.status, deviceId: receiverId, setup: false };
    receiver.config = structuredClone(device.config);
    receiver.config.role = "rx";
    receiver.config.peers = [
      {
        deviceId: device.status.deviceId,
        pairId: device.config.peers[0].pairId,
        name: "Controller",
        adminAuthorized: true,
      },
    ];
    receiver.config.revision = 7;
    if (view === "receipt") {
      receiver.pendingApply = "e".repeat(32);
      message =
        "Previous Apply outcome unknown. Check the saved receiver receipt; no change will be replayed.";
      isError = true;
    } else draft = structuredClone(receiver.config);
  }
  if (view === "edit") draft = structuredClone(device.config);
}
render();
