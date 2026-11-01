import { capabilities } from "./capabilities";
import { SerialTransport } from "./serial";
import { desktop, nativeInvoke, NativeSerialPort, type PortInfo } from "./native";
import {
  DeviceSession,
  keepPairs,
  newAction,
  MAX_ACTIONS,
  parseCloudPairing,
  type Config,
  type Action,
  type Secrets,
} from "./protocol";
import fixture from "../../contracts/fixtures/config-default.json";

export function mountSetup(root: HTMLDivElement) {
if (!capabilities.canConfigureDevices) {
  root.innerHTML = '<main><h1>Configure</h1><p class="notice">USB setup is disabled in this browser. Use desktop Google Chrome over HTTPS, or the UberClicker desktop app.</p><button type="button" disabled>Connect device</button></main>';
  return;
}
const element = (id: string) => root.querySelector<HTMLElement>(`[id="${id}"]`);
let ports: PortInfo[] | undefined;
let device: DeviceSession | undefined,
  draft: Config | undefined,
  busy = false,
  message = "",
  isError = false;
function selected() { return device!; }
let wifiEdits: { op: string; value: string }[] = [];
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
const commonKeys = new Map([
  [80, "Left Arrow"], [79, "Right Arrow"], [75, "Page Up"], [78, "Page Down"],
  [82, "Up Arrow"], [81, "Down Arrow"], [44, "Space"], [40, "Enter"],
  [41, "Escape"], [42, "Backspace"], [19, "P (previous)"], [17, "N (next)"],
]);
const actionLabels: Record<Action["type"], string> = {
  lora: "LoRa", cloud: "Cloud", ble_keyboard: "Bluetooth keyboard",
  ble_midi: "Bluetooth MIDI", osc_udp: "OSC", udp: "UDP", tcp: "TCP",
};
const events = ["previous", "next"] as const;
type Event = typeof events[number];
function actionSummary(c: Config) {
  const p = c.profiles[c.activeProfile];
  return events.map(event => `${event === "previous" ? "Previous" : "Next"}: ${p[event].filter(a => a.enabled).map(a => actionLabels[a.type]).join(" + ") || "no actions"}`).join(" · ");
}
function notify(s: string, error = false) {
  message = s;
  isError = error;
}
function lost() {
  device?.lost();
  device = undefined;
  draft = undefined;
  wifiEdits = [];
  notify(
    "Disconnected. Reconnect by USB serial and reread settings before editing again. An unfinished edit expires after two minutes without activity.",
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
  try {
    await fn();
  } catch (e) {
    const errors: Record<string, string> = {
      RECOVERY_REQUIRED:
        "Stored settings need physical recovery. Use the clicker factory-reset menu; no settings were erased automatically.",
      TRANSACTION_CONFLICT:
        "This Apply identifier was reused with different content. Resolve the previous result before a new edit.",
      SETUP_REQUIRED:
        "Reconnect by USB serial and reread settings.",
      UNAUTHORIZED: "Reconnect by USB serial and reread settings.",
      UNSUPPORTED: "This operation needs the new direct-setup firmware. Update the connected device.",
      SESSION_EXPIRED: "Setup expired. Reread settings over USB serial and start the edit again.",
      STALE_REVISION:
        "Settings changed. Cancel and reread before editing again.",
      INVALID:
        "Some settings are invalid. Check names, endpoints, mappings, and secret actions.",
      TOO_LARGE: "Settings are too large for this device. Remove an action or Wi-Fi network, or shorten message text, then try again.",
      PERSIST_FAILED:
        "The device could not save settings. Reconnect and reread before retrying.",
      BUSY: "The device is busy. Finish or cancel an open edit, or wait for its two-minute idle timeout. Close any on-device menu before trying updates.",
      RATE_LIMITED: "Too many requests. Wait before trying again.",

    };
    const reason = e instanceof Error ? e.message : "Action failed";
    if (["UNAUTHORIZED", "SETUP_REQUIRED", "SESSION_EXPIRED"].includes(reason)) {
      device?.lost();
      draft = undefined;
      wifiEdits = [];
    }
    notify(errors[reason] ?? reason, true);
  } finally {
    if (!device) draft = undefined;
    busy = false;
    render();
  }
}
function actionForm(event: Event, a: Action, index: number, count: number) {
  const name = `${event}-${index}`, output = a.type;
  let fields = "";
  if (output === "ble_keyboard") {
    const custom = !commonKeys.has(a.key);
    fields = `<label>Key<select id="${name}-keyChoice">${Array.from(commonKeys, ([key, label]) => `<option value="${key}" ${key === a.key ? "selected" : ""}>${label}</option>`).join("")}<option value="custom" ${custom ? "selected" : ""}>Custom HID usage</option></select></label>` +
      `<details ${custom || a.modifiers ? "open" : ""}><summary>Advanced keyboard mapping</summary><div class="fields">${input("HID usage", `${name}-key`, a.key, "number", 'min="0" max="101" required')}${input("Modifier bits", `${name}-modifiers`, a.modifiers, "number", 'min="0" max="255" required')}</div></details>`;
  } else if (output === "ble_midi") {
    fields = select("MIDI message", `${name}-midiType`, a.midi.type, ["note", "cc", "program"]) +
      input("MIDI channel", `${name}-channel`, a.midi.channel, "number", 'min="1" max="16" required') +
      input("Note / controller / program number", `${name}-number`, a.midi.number, "number", 'min="0" max="127" required') +
      input("MIDI value", `${name}-value`, a.midi.value, "number", 'min="0" max="127" required');
  } else if (output === "osc_udp") {
    fields = input("OSC address", `${name}-osc`, a.osc, "text", 'maxlength="64"') +
      input("OSC integer value", `${name}-value`, a.midi.value, "number", 'min="0" max="127"');
  } else if (output === "udp" || output === "tcp") {
    fields = input("Message text", `${name}-payload`, a.payload, "text", 'maxlength="128"');
  }
  if (["osc_udp", "udp", "tcp"].includes(output)) {
    fields = input("Destination host / IPv4", `${name}-host`, a.host, "text", 'required maxlength="63"') +
      input("Destination port", `${name}-port`, a.port, "number", 'min="1" max="65535" required') + fields;
  }
  const hint = output === "lora" ? "Broadcast to receivers. Delivery cannot be confirmed." :
    output === "cloud" ? "Requires a channel connection and Wi-Fi." :
    output === "tcp" ? "Sends the message followed by a newline over a new TCP connection." :
    output === "udp" ? "Sends the message as one UDP packet." :
    output === "osc_udp" ? "Sends one OSC message with an integer value to this destination." :
    output === "ble_midi" ? "Pair from the device’s Bluetooth menu. Notes release automatically." :
    "Pair your computer from the device’s Bluetooth menu.";
  return `<details id="${name}-action" class="event-action ${a.enabled ? "" : "action-disabled"}" ${a.enabled ? "open" : ""}><summary>${index + 1}. ${actionLabels[output]}${a.enabled ? "" : " · Off"}</summary>
    <div class="action-heading"><label class="action-toggle"><input id="${name}-enabled" type="checkbox" ${a.enabled ? "checked" : ""}>Enabled<span class="sr-only"> ${event} action ${index + 1}</span></label></div>
    <p class="hint">${hint}</p><div class="fields">${fields}</div>
    <details id="${name}-options" class="action-options"><summary>Action options</summary><div class="action-controls"><button type="button" data-action="up" data-event="${event}" data-index="${index}" class="text-button" ${index === 0 ? "disabled" : ""} aria-label="Move ${event} action ${index + 1} up">Move up</button><button type="button" data-action="down" data-event="${event}" data-index="${index}" class="text-button" ${index === count - 1 ? "disabled" : ""} aria-label="Move ${event} action ${index + 1} down">Move down</button><button type="button" data-action="duplicate" data-event="${event}" data-index="${index}" class="text-button" ${count >= MAX_ACTIONS ? "disabled" : ""}>Duplicate<span class="sr-only"> ${event} action ${index + 1}</span></button><button type="button" data-action="remove" data-event="${event}" data-index="${index}" class="text-button">Remove<span class="sr-only"> ${event} action ${index + 1}</span></button></div></details>
  </details>`;
}
function eventForm(event: Event, actions: Action[], role: Config["role"]) {
  return `<fieldset class="event-config"><legend>${event === "previous" ? "Previous" : "Next"} button</legend>
    <p class="hint">Actions run in order.${role === "rx" ? " Triggered by this event from the selected transmitter." : ""}</p>
    ${actions.map((a, i) => actionForm(event, a, i, actions.length)).join("") || '<p>No actions. Add one below.</p>'}
    ${actions.length < MAX_ACTIONS ? `<details class="action-picker"><summary>Add action</summary><div class="action-types">${Object.entries(actionLabels).filter(([type]) => role === "tx" || (type !== "lora" && type !== "cloud")).map(([type, label]) => `<button type="button" class="secondary" data-add-action="${type}" data-event="${event}">${label}</button>`).join("")}</div></details>` : `<p class="hint">Maximum ${MAX_ACTIONS} actions per event.</p>`}
  </fieldset>`;
}

function render() {
  const s = device?.status, c = draft || device?.config;
  const expanded = Array.from(root.querySelectorAll<HTMLDetailsElement>("details[id]")).map(d => [d.id, d.open] as const);
  root.innerHTML = `<main>
  <div class="intro"><h1>${draft ? "Edit clicker" : "Configure"}</h1></div>
  ${preview ? '<p class="notice">Preview — sample device. Changes here are not saved to hardware.</p>' : ""}
  <p id="message" class="notice ${isError ? "error" : ""}" role="status" aria-live="polite">${escape(message)}</p>
  ${!s ? device ? '<section><h2>Reading your device</h2><p>Reading status and settings over USB serial…</p><button id="disconnect">Disconnect</button></section>' : `<section><h2>Connect your device</h2><p>Connect a clicker or receiver with a USB data cable.</p><p class="hint">Reading settings keeps cues running. Editing pauses them until you apply or cancel changes.</p>${desktop && ports ? `<div class="fields"><label>USB device<select id="serialPort">${ports.map(port => `<option value="${escape(port.path)}">${escape(port.label)}</option>`).join("")}</select></label></div><button id="openSerial" ${ports.length ? "" : "disabled"}>Connect device</button>` : ""}<button id="serial" ${(desktop || SerialTransport.supported()) ? "" : "disabled"}>${desktop ? ports ? "Refresh USB devices" : "Choose USB device" : "Connect device"}</button></section>` : `
  <section class="identity"><div><h2>${escape(c?.profiles[c.activeProfile].name || "UberClicker")}</h2><p>${c?.role === "rx" ? "Receiver" : "Transmitter"}</p><details id="device-info"><summary>Device details</summary><p>ID: ${escape(s.deviceId)}</p><p>Firmware: ${escape(s.firmwareVersion)} · Revision ${c?.revision ?? s.revision}</p></details></div><span class="state">${draft ? "Editing · cues paused" : "Connected"}</span></section>
  ${s.recoveryRequired ? '<p class="notice error">Stored settings need physical recovery.</p>' : !c ? '<p>Reread settings to continue.</p>' : draft ? editor(draft) : `
  <section><h2>Button actions</h2><p>${c.role === "tx" ? "LoRa broadcasts cannot confirm delivery." : "Select a transmitter with B1/B2 on this receiver."}</p><p>${escape(actionSummary(c))}</p><button id="edit">Edit settings</button></section>`}
  ${c && !draft && s.capabilities.serial_config && s.capabilities.ota_session ? '<button class="secondary" id="updates">Firmware update</button>' : ""}
  ${c?.role === "tx" && !draft && s.capabilities.cloud_output ? `<details id="cloud-connection"><summary>Cloud Channel${s.cloud?.configured ? " · Paired" : ""}</summary><p>${s.cloud?.configured ? `Channel: ${escape(s.cloud.room)}.` : "Enable Cloud actions and save Wi-Fi in Edit settings. The clicker displays a code to enter in Cloud Channels."}</p><p><a href="#account">Open Cloud Channels</a> to link this clicker and choose its routes.</p><details><summary>Manual connection</summary>${desktop ? `<button id="pairLocalRoom">Use this computer’s channel</button><p class="hint">Connect a channel in Listen and pause output before pairing.</p>` : ""}<form id="cloud-pairing"><label>Clicker code<input id="cloudCode" type="password" autocomplete="off" maxlength="256" required></label><button type="submit">Save channel connection</button><button type="button" id="clearCloud" class="secondary">Reset account connection</button></form><p class="hint">Add Cloud actions and Wi-Fi in device settings to send clicks.</p></details></details>` : ""}
  <div class="footer-actions"><button id="refresh" class="secondary">Refresh settings</button><button id="disconnect" class="text-button">Disconnect</button></div>`}
  </main>`;
  for (const [id, open] of expanded) { const details = element(id) as HTMLDetailsElement | null; if (details) details.open = open; }
  bind();
  if (busy) for (const el of root.querySelectorAll<HTMLButtonElement | HTMLInputElement | HTMLSelectElement>("button,input,select")) if (el.id !== "disconnect") el.disabled = true;
  if (preview) for (const button of root.querySelectorAll<HTMLButtonElement>("button")) {
    if (!button.hasAttribute("data-add-action") && !button.hasAttribute("data-action") && !button.hasAttribute("data-profile") && !button.hasAttribute("data-remove-wifi") && !["addProfile", "removeProfile", "addWifi"].includes(button.id)) button.disabled = true;
  }
}
function editor(c: Config) {
  const p = c.profiles[c.activeProfile];
  return `<form id="editor">
    <section><h2>Button actions</h2><details id="profiles"><summary>Profile: ${escape(p.name)}</summary><div class="profile-tabs">${c.profiles.map((p, i) => `<button type="button" data-profile="${i}" aria-pressed="${i === c.activeProfile}" class="${i === c.activeProfile ? "selected" : "secondary"}">${escape(p.name)}</button>`).join("")}<button type="button" id="addProfile" class="text-button" ${c.profiles.length >= 4 ? "disabled" : ""}>Add profile</button>${c.profiles.length > 1 ? '<button type="button" id="removeProfile" class="text-button">Remove active profile</button>' : ""}</div></details>
    <div class="fields">${input("Device name", "deviceName", p.name, "text", 'required maxlength="24"')}</div>
    <p class="hint">Shown on the device${c.role === "tx" ? " and receiver lists" : ""}. Maximum 24 UTF-8 bytes.</p>

    ${events.map(event => eventForm(event, p[event], c.role)).join("")}</section>
    <details id="radio-settings"><summary>Radio settings</summary><div class="fields">${select("Radio preset", "radio", c.radio, ["balanced", "long_range"])}</div><p class="hint">Use the same preset on TX and RX. The installed firmware determines the device’s role.</p></details>
    <details id="wifi-settings"><summary>Wi-Fi networks${c.wifi.length ? ` · ${c.wifi.length} saved` : ""}</summary><p>For network outputs and updates. Networks are tried in order.</p>
    ${c.wifi.map((w, i) => {
      const edit = wifiEdits[i] ?? { op: "keep", value: "" };
      return `<fieldset><legend>Network ${i + 1}</legend><div class="fields">${input("Network name", `ssid-${i}`, w.ssid, "text", 'required maxlength="32"')}${select("Password action", `wifiOp-${i}`, edit.op, ["keep", "replace", "clear"])}${edit.op === "replace" ? input("New password", `wifiPassword-${i}`, edit.value, "password", 'required minlength="8" maxlength="63" autocomplete="new-password"') : ""}</div><button type="button" data-remove-wifi="${i}" class="text-button">Remove network ${i + 1}</button></fieldset>`;
    }).join("")}
    ${c.wifi.length ? "" : '<p class="hint">No networks saved.</p>'}<button type="button" id="addWifi" class="text-button">Add Wi-Fi network</button><p class="hint">Keep preserves the saved password. If you rename a network, choose Replace or Clear. Clear connects without a password.</p></details>
    <section class="apply-bar"><p>Apply or cancel to resume cues. Unsaved edits expire after two minutes without activity.</p><button type="submit">Apply changes</button><button id="cancel" type="button" class="secondary">Cancel</button></section></form>`;
}
function value(id: string) {
  return (element(id) as HTMLInputElement).value;
}
function capture() {
  if (!draft) return;
  draft.radio = value("radio") as Config["radio"];
  draft.wifi.forEach((network, i) => {
    network.ssid = value(`ssid-${i}`);
    wifiEdits[i] = { op: value(`wifiOp-${i}`), value: element(`wifiPassword-${i}`) ? value(`wifiPassword-${i}`) : wifiEdits[i]?.value ?? "" };
  });
  const p = draft.profiles[draft.activeProfile];
  p.name = value("deviceName");
  for (const event of events) for (const [index, a] of p[event].entries()) {
    const name = `${event}-${index}`;
    a.enabled = (element(`${name}-enabled`) as HTMLInputElement).checked;
    if (element(`${name}-host`)) a.host = value(`${name}-host`);
    if (element(`${name}-port`)) a.port = Number(value(`${name}-port`));
    if (element(`${name}-key`)) a.key = Number(value(`${name}-key`));
    if (element(`${name}-modifiers`)) a.modifiers = Number(value(`${name}-modifiers`));
    if (element(`${name}-value`)) a.midi.value = Number(value(`${name}-value`));
    if (element(`${name}-midiType`)) a.midi.type = value(`${name}-midiType`) as typeof a.midi.type;
    if (element(`${name}-channel`)) a.midi.channel = Number(value(`${name}-channel`));
    if (element(`${name}-number`)) a.midi.number = Number(value(`${name}-number`));
    if (element(`${name}-osc`)) a.osc = value(`${name}-osc`);
    if (element(`${name}-payload`)) a.payload = value(`${name}-payload`);
  }
}
function on(id: string, fn: () => Promise<void>) {
  element(id)?.addEventListener("click", () => void run(fn));
}
async function connected(transport: SerialTransport) {
  const session = new DeviceSession(transport);
  device = session;
  notify("Reading settings…");
  render();
  try {
    await session.refresh();
    if (!session.status?.recoveryRequired) await session.read();
    notify(session.status?.recoveryRequired ? "Connected by USB serial. Stored settings need physical recovery." : "Settings loaded.");
  } catch (e) { await transport.disconnect(); throw e; }

}
function bind() {
  root.querySelector<HTMLFormElement>("#cloud-pairing")?.addEventListener("submit", e => {
    e.preventDefault();
    const code = value("cloudCode");
    try { parseCloudPairing(code); } catch (error) { notify(error instanceof Error ? error.message : "Invalid pairing code", true); render(); return; }
    void run(async () => { await selected().configureCloud(code); notify("Cloud Channel saved. Add Cloud actions and Wi-Fi in settings, then enable output in the companion."); });
  });
  on("clearCloud", async () => { await selected().configureCloud(null); notify("Account connection reset. A new code appears when Cloud actions and Wi-Fi are enabled."); });
  for (const event of events) for (const index of draft?.profiles[draft.activeProfile][event].keys() ?? []) {
    const name = `${event}-${index}`;
    const choice = element(`${name}-keyChoice`) as HTMLSelectElement | null;
    const usage = element(`${name}-key`) as HTMLInputElement | null;
    choice?.addEventListener("change", () => {
      if (choice.value === "custom") {
        usage!.closest("details")!.open = true;
        usage!.focus();
      } else {
        usage!.value = choice.value;
        capture();
      }
    });
    usage?.addEventListener("input", () => {
      choice!.value = usage.value !== "" && commonKeys.has(Number(usage.value)) ? usage.value : "custom";
    });
  }
  for (const id of draft?.wifi.map((_, i) => `wifiOp-${i}`) ?? []) {
    element(id)?.addEventListener("change", () => { capture(); render(); });
  }
  root.querySelectorAll<HTMLButtonElement>("[data-add-action]").forEach(button => button.addEventListener("click", () => {
    capture();
    const event = button.dataset.event as Event, actions = draft!.profiles[draft!.activeProfile][event];
    if (actions.length < MAX_ACTIONS) actions.push(newAction(button.dataset.addAction as Action["type"], event));
    render();
    element(`${event}-${actions.length - 1}-enabled`)?.focus();
  }));
  root.querySelectorAll<HTMLButtonElement>("[data-action]").forEach(button => button.addEventListener("click", () => {
    capture();
    const event = button.dataset.event as Event, actions = draft!.profiles[draft!.activeProfile][event];
    let index = Number(button.dataset.index);
    switch (button.dataset.action) {
      case "remove": actions.splice(index, 1); break;
      case "duplicate": if (actions.length < MAX_ACTIONS) actions.splice(index + 1, 0, structuredClone(actions[index])); break;
      case "up": if (index > 0) { [actions[index - 1], actions[index]] = [actions[index], actions[index - 1]]; index--; } break;
      case "down": if (index < actions.length - 1) { [actions[index + 1], actions[index]] = [actions[index], actions[index + 1]]; index++; } break;
    }
    render();
    element(`${event}-${Math.min(index, actions.length - 1)}-enabled`)?.focus();
  }));
  root.querySelectorAll<HTMLInputElement>('.action-toggle input').forEach(toggle => toggle.addEventListener("change", () => { capture(); render(); element(toggle.id)?.focus(); }));
  on("serial", async () => {
    if (desktop) {
      ports = await nativeInvoke<PortInfo[]>("serial_ports");
      notify(ports.length ? "Choose the USB device you want to configure." : "No USB serial devices found. Connect your clicker with a data cable, then refresh.");
    } else await connected(await SerialTransport.discover(lost));
  });
  on("openSerial", async () => {
    const port = await NativeSerialPort.open(value("serialPort"));
    await connected(await SerialTransport.connect(port, lost));
  });
  on("pairLocalRoom", async () => {
    const code = await nativeInvoke<string>("pairing");
    await selected().configureCloud(code);
    notify("Paired to this computer’s channel. Add Cloud actions and Wi-Fi in settings, then enable output in Listen.");
  });
  on("updates", async () => {
    await device!.openUpdateSession();
    await device!.refresh();
    notify("Update screen requested. On the clicker, press Check, review the version, then Install. Nothing was downloaded or written by this request.");
  });
  on("edit", async () => {
    draft = await selected().begin();
    wifiEdits = [];
    notify("Editing this device. Cues are paused.");
  });
  on("refresh", async () => {
    if (draft) throw Error("Apply or cancel before rereading");
    await selected().refresh();
    await selected().read();
    notify("Committed settings reread.");
  });
  element("disconnect")?.addEventListener("click", () => {
    const session = device;
    if (!session) return;
    if (!busy && session.session) {
      void run(async () => {
        try { await session.cancel(); }
        finally { await session.transport.disconnect(); }
      });
    } else void session.transport.disconnect().catch(lost);
  });
  on("cancel", async () => {
    try {
      await selected().cancel();
    } finally {
      draft = undefined;
      wifiEdits = [];
    }
    notify("Changes discarded. Button output resumed.");
  });
  root.querySelector("#editor")?.addEventListener("submit", (e) => {
    e.preventDefault();
    if (!draft || busy) return;
    capture();
    const secrets: Secrets = {
      wifi: draft.wifi.map((network, i) => {
        const edit = wifiEdits[i];
        network.configured = edit.op === "keep"
          ? selected().config!.wifi.find((saved) => saved.ssid === network.ssid)?.configured ?? false
          : edit.op === "replace";
        return edit.op === "replace" ? { ssid: network.ssid, op: "replace", value: edit.value }
          : { ssid: network.ssid, op: edit.op as "keep" | "clear" };
      }),
      pairs: keepPairs(selected().config!),
    };
    const targetSession = selected(),
      configToApply = draft!;
    void run(async () => {
      try {
        await targetSession.apply(configToApply, secrets);
      } finally {
        if (!targetSession.session) {
          draft = undefined;
          wifiEdits = [];
        }
      }
      draft = undefined;
      notify("Changes saved on this device. Button output resumed.");
    });
  });
  root.querySelectorAll<HTMLButtonElement>("[data-profile]").forEach((b) =>
    b.addEventListener("click", () => {
      capture();
      draft!.activeProfile = Number(b.dataset.profile);
      render();
    }),
  );
  on("addWifi", async () => {
    capture();
    draft!.wifi.push({ ssid: "", configured: false });
    wifiEdits.push({ op: "replace", value: "" });
  });
  root.querySelectorAll<HTMLButtonElement>("[data-remove-wifi]").forEach((button) => {
    button.addEventListener("click", () => {
      capture();
      const i = Number(button.dataset.removeWifi);
      draft!.wifi.splice(i, 1);
      wifiEdits.splice(i, 1);
      render();
    });
  });
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
}
if (preview) {
  device = new DeviceSession({ request: async () => { throw Error("Preview only"); }, disconnect: async () => {} });
  device.config = structuredClone(fixture) as Config;
  device.status = { deviceId: "aabbccddeeff", boardId: "thinknode_m5", firmwareVersion: "preview", schemaVersion: 4, protocolVersion: 2, revision: 0, setup: false, editing: false, claimed: true, capabilities: { ble_config: false, serial_config: true, lan_config: false, setup_hotspot: false, outputs: [] } };
  const view = new URLSearchParams(location.search).get("preview");
  if (view === "rx") {
    device.config.role = "rx";
    for (const event of events) device.config.profiles[0][event] = [newAction("ble_keyboard", event)];
  }
  if (view === "osc" || view === "midi") {
    for (const event of events) device.config.profiles[0][event] = view === "osc" ?
      [newAction("lora", event), { ...newAction("osc_udp", event), host: "192.168.1.10" }, { ...newAction("osc_udp", event), host: "192.168.1.20", port: 9000 }] : [newAction("ble_midi", event)];
  }
  device.status.capabilities.fixed_role = device.config.role;
  if (["edit", "rx", "osc", "midi"].includes(view || "")) draft = structuredClone(device.config);
}
render();

}
