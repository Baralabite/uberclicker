import { desktop as native, nativeInvoke as invoke } from "./native";
import "./receiver.css";

export function mountReceiver(root: HTMLDivElement) {
const element = (id: string) => root.querySelector<HTMLElement>(`[id="${id}"]`);

type Mapping = { keyboard: { enabled: boolean; key: string; modifiers: string[] }; osc: { enabled: boolean; host: string; port: number; address: string; value: number } };
type NameRule = { kind: "contains" | "regex"; pattern: string };
type Assignment = { transmitterId: string; name: string; presentationIds: string[]; nameRules: NameRule[] };
type ProPresenter = { enabled: boolean; host: string; port: number; assignments: Assignment[] };
type Presentation = { id: string; name: string; library: string };
type Settings = { proPresenter: ProPresenter; outputOnStartup: boolean; previous: Mapping; next: Mapping };
type Status = { connected: boolean; armed: boolean; room: string; received: number; lastEvent: string; error: string; relayRttMs: number | null; lastTransmitter: string };
type View = { settings: Settings; status: Status; api: string };
const makeMapping = (key: string, address: string): Mapping => ({ keyboard: { enabled: true, key, modifiers: [] }, osc: { enabled: false, host: "127.0.0.1", port: 8000, address, value: 1 } });
let state: View = { settings: { proPresenter: { enabled: false, host: "127.0.0.1", port: 50001, assignments: [] }, outputOnStartup: true, previous: makeMapping("LeftArrow", "/previous"), next: makeMapping("RightArrow", "/next") }, status: { connected: false, armed: false, room: "", received: 0, lastEvent: "No clicks received", error: "", relayRttMs: null, lastTransmitter: "" }, api: "https://api.uberclicker.com" };
let startOnBoot: boolean | null = null;
let busy = false, dirty = false;
let presentations: Presentation[] = [];
const keys: [string, string][] = [["LeftArrow", "Left arrow"], ["RightArrow", "Right arrow"], ["PageUp", "Page up"], ["PageDown", "Page down"], ["UpArrow", "Up arrow"], ["DownArrow", "Down arrow"], ["Space", "Space"], ["Return", "Enter"], ["Escape", "Escape"], ["Home", "Home"], ["End", "End"], ["Tab", "Tab"], ["Backspace", "Backspace"], ["F5", "F5"], ["b", "B"], ["n", "N"], ["p", "P"], ["w", "W"]];
const escape = (s: unknown) => String(s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]!);
function mappingForm(button: "previous" | "next") {
  const m = state.settings[button];
  return `<fieldset class="mapping"><legend><span class="button-symbol" aria-hidden="true">${button === "previous" ? "◀" : "▶"}</span>${button === "previous" ? "Previous" : "Next"}</legend>
    <label class="toggle"><input type="checkbox" name="${button}-keyboard" ${m.keyboard.enabled ? "checked" : ""}>Keyboard</label>
    <label>Key<select name="${button}-key">${keys.map(([key, name]) => `<option value="${key}" ${m.keyboard.key === key ? "selected" : ""}>${name}</option>`).join("")}</select></label>
    <div class="modifiers" role="group" aria-label="${button} key modifiers">${["Control", "Alt", "Shift", "Meta"].map(mod => `<label><input type="checkbox" name="${button}-${mod}" ${m.keyboard.modifiers.includes(mod) ? "checked" : ""}>${mod === "Meta" ? "Cmd / Win" : mod === "Control" ? "Ctrl" : mod}</label>`).join("")}</div>
    <details class="osc-settings" ${m.osc.enabled ? "open" : ""}><summary>OSC</summary><label class="toggle osc-toggle"><input type="checkbox" name="${button}-osc" ${m.osc.enabled ? "checked" : ""}>Enable OSC</label>
    <div class="destination"><label>Host<input name="${button}-host" value="${escape(m.osc.host)}" required maxlength="64" spellcheck="false"></label><label>Port<input name="${button}-port" type="number" min="1" max="65535" value="${m.osc.port}" required></label></div>
    <div class="destination"><label>Address<input name="${button}-address" value="${escape(m.osc.address)}" pattern="/[A-Za-z0-9/_-]*" maxlength="64" required spellcheck="false"></label><label>Integer<input name="${button}-value" type="number" min="-2147483648" max="2147483647" value="${m.osc.value}" required></label></div></details>
  </fieldset>`;
}
function render() {
  root.innerHTML = `<main><section class="connection" aria-labelledby="connection-title"><div class="section-heading"><h1 id="connection-title">Listen</h1><span id="connection-state" class="badge">Disconnected</span></div>
  ${!native ? '<p class="preview">Browser preview. Open the desktop app to connect and send output.</p>' : ""}
  <details id="receiver-connection" class="receiver-options" ${state.status.room ? "" : "open"}><summary>Cloud Channel</summary><div class="room-line"><span>Channel</span><span id="room">No channel connected</span></div>
  <p>Create a channel in your <a href="#account">account</a>, then paste its Client Companion code.</p><label>Client Companion code<textarea id="room-import" rows="3" spellcheck="false" autocomplete="off"></textarea></label><div class="connection-actions"><button id="create">Listen to channel</button><button class="secondary" id="reconnect">Reconnect</button><button class="text" id="disconnect">Disconnect</button></div>
  <details class="pairing"><summary>Connect a clicker</summary><p>Open Cue on your phone, or enable Cloud actions and Wi-Fi on your transmitter. Enter its displayed pairing code in Cloud under Cue Remote → Add, then select this channel for the clicker.</p><details><summary>Manual USB transmitter connection</summary><p>Pause output to show the code. Paste it in Configure → Cloud Channel → Manual connection. Add Cloud actions and Wi-Fi to the transmitter.</p><button class="secondary" id="pair">Show Clicker code</button><label id="pairing-label" hidden>Clicker code<textarea id="pairing" readonly rows="3" spellcheck="false"></textarea></label><p class="small">Treat this code like a password. It lets a TX send clicks to this channel.</p></details></details>
  </details></section>
  <section class="output-section" aria-labelledby="output-title"><div class="section-heading"><h2 id="output-title">Output</h2><button class="arm" id="arm">Enable output</button></div><p class="small" id="output-hint">Output is paused. Check output settings, then enable.</p>
  <details class="receiver-options" id="output-settings"><summary>Output settings</summary><form id="mappings">
  <details class="propresenter-settings" ${state.settings.proPresenter.enabled ? "open" : ""}><summary>ProPresenter restrictions${state.settings.proPresenter.enabled ? " · Enabled" : ""}</summary><fieldset class="propresenter"><legend class="sr-only">ProPresenter</legend>
  <label class="toggle"><input type="checkbox" id="pp-enabled" ${state.settings.proPresenter.enabled ? "checked" : ""}>Use ProPresenter presentation restrictions</label>
  <p class="small">Replaces keyboard and OSC. Only assigned transmitters can control allowed presentations. Empty assignments block all presentations.</p>
  <div class="destination"><label>ProPresenter host<input id="pp-host" value="${escape(state.settings.proPresenter.host)}" required maxlength="64" spellcheck="false"></label><label>API port<input id="pp-port" type="number" min="1" max="65535" value="${state.settings.proPresenter.port}" required></label></div>
  <p class="small">Enable Network in ProPresenter Settings and enter the port shown there.</p>
  <button type="button" class="secondary" id="pp-fetch">Fetch presentations</button><p class="small" id="pp-catalog" role="status">Fetch presentations to choose from your libraries.</p>
  <div id="pp-assignments"></div><button type="button" class="secondary" id="pp-add">Add transmitter</button>
  <p class="small">Enter the transmitter’s 12-character device ID. To identify one by a click, save and enable ProPresenter mode, press its button, then pause output. Unassigned transmitters are blocked.</p>
  <p class="small" id="pp-last"></p>
  </fieldset></details><div class="mappings">${mappingForm("previous")}${mappingForm("next")}</div><div class="save-line"><p class="small">Keyboard controls the focused app. OSC sends one integer to localhost or an IP address.</p><button class="secondary" id="save" type="submit">Save mappings</button></div></form></details>
  </section>
  <details class="startup-section"><summary>Startup settings</summary>
  <label class="toggle"><input type="checkbox" id="output-on-startup">Enable output on startup</label>
  <p class="small">Applies when you next open the app and connect to your saved channel. Turn off to start paused.</p>
  <label class="toggle"><input type="checkbox" id="start-on-boot">Start on boot</label>
  <p class="small">Open Companion when you sign in. Changes save immediately.</p></details>
  <section class="activity" aria-label="Activity"><span id="last-event">No clicks received</span><span id="received">0 clicks</span><span id="rtt"></span></section>
  <p id="message" role="status" aria-live="polite"></p>
  <footer>Companion keeps receiving when you close the window. After a disconnect, enable output to resume.</footer></main>`;
  element("create")!.onclick = () => {
    if (state.status.room && !confirm("Replace this computer’s channel connection?")) return;
    void action("import_room", { code: (element("room-import") as HTMLTextAreaElement).value }, () => { (element("room-import") as HTMLTextAreaElement).value = ""; (element("receiver-connection") as HTMLDetailsElement).open = false; });
  };
  element("reconnect")!.onclick = () => void action("reconnect");
  element("disconnect")!.onclick = () => void action("disconnect");
  element("arm")!.onclick = () => void action("arm", { armed: !state.status.armed });
  (element("output-on-startup") as HTMLInputElement).onchange = e => void action("set_output_on_startup", { enabled: (e.target as HTMLInputElement).checked });
  (element("start-on-boot") as HTMLInputElement).onchange = e => void action("set_start_on_boot", { enabled: (e.target as HTMLInputElement).checked }, value => { startOnBoot = Boolean(value); });
  element("pair")!.onclick = async () => {
    await action("pairing", {}, result => {
      (element("pairing") as HTMLTextAreaElement).value = String(result);
      element("pairing-label")!.hidden = false;
    });
  };
  renderAssignments(state.settings.proPresenter.assignments);
  element("pp-add")!.onclick = () => {
    const assignments = readAssignments();
    assignments.push({ transmitterId: state.status.lastTransmitter, name: `Tx ${assignments.length + 1}`, presentationIds: [], nameRules: [] });
    renderAssignments(assignments); dirty = true; update();
  };
  element("pp-fetch")!.onclick = () => {
    const config = readProPresenter();
    // Fetching the catalog must work before a newly added transmitter has an ID.
    void action("propresenter_presentations", { config: { ...config, assignments: [] } }, result => {
      presentations = result as Presentation[];
      renderAssignments(config.assignments);
      element("pp-catalog")!.textContent = `${presentations.length} presentations fetched from ${config.host}:${config.port}. Hold Ctrl / Cmd to select multiple presentations.`;
    });
  };
  const form = element("mappings") as HTMLFormElement;
  form.oninput = () => { dirty = true; update(); };
  form.onsubmit = e => {
    e.preventDefault();
    const data = new FormData(form), settings = structuredClone(state.settings);
    settings.proPresenter = readProPresenter();
    if (!settings.proPresenter.enabled) for (const button of ["previous", "next"] as const) {
      settings[button] = { keyboard: { enabled: data.has(`${button}-keyboard`), key: String(data.get(`${button}-key`)), modifiers: ["Control", "Alt", "Shift", "Meta"].filter(mod => data.has(`${button}-${mod}`)) }, osc: { enabled: data.has(`${button}-osc`), host: String(data.get(`${button}-host`)), port: Number(data.get(`${button}-port`)), address: String(data.get(`${button}-address`)), value: Number(data.get(`${button}-value`)) } };
    }
    void action("save_settings", { settings }, () => { state.settings = settings; dirty = false; message("Mappings saved."); });
  };
  update();
}
function readAssignments(): Assignment[] {
  return Array.from(root.querySelectorAll<HTMLElement>(".pp-assignment")).map(row => ({
    name: row.querySelector<HTMLInputElement>(".pp-name")!.value.trim(),
    transmitterId: row.querySelector<HTMLInputElement>(".pp-id")!.value.trim().toLowerCase(),
    presentationIds: Array.from(row.querySelector<HTMLSelectElement>(".pp-presentations")!.selectedOptions).map(option => option.value),
    nameRules: Array.from(row.querySelectorAll<HTMLElement>(".pp-name-rule")).map(rule => ({
      kind: rule.querySelector<HTMLSelectElement>(".pp-rule-kind")!.value as NameRule["kind"],
      pattern: rule.querySelector<HTMLInputElement>(".pp-rule-pattern")!.value,
    })),
  }));
}
function readProPresenter(): ProPresenter {
  return { enabled: (element("pp-enabled") as HTMLInputElement).checked,
    host: (element("pp-host") as HTMLInputElement).value.trim(),
    port: Number((element("pp-port") as HTMLInputElement).value), assignments: readAssignments() };
}
function nameRuleForm(rule: NameRule) {
  return `<div class="pp-name-rule"><label>Match type<select class="pp-rule-kind"><option value="contains" ${rule.kind === "contains" ? "selected" : ""}>Name contains</option><option value="regex" ${rule.kind === "regex" ? "selected" : ""}>Name matches regex</option></select></label>
    <label>Pattern<input class="pp-rule-pattern" value="${escape(rule.pattern)}" required maxlength="512" spellcheck="false" placeholder="Sermon or ^Sunday.*"></label>
    <button type="button" class="text pp-rule-remove">Remove rule</button></div>`;
}
function renderAssignments(assignments: Assignment[]) {
  element("pp-assignments")!.innerHTML = assignments.map((a, index) => {
    const options = [...presentations];
    for (const id of a.presentationIds) {
      if (!options.some(p => p.id.toLowerCase() === id.toLowerCase())) options.push({ id, name: "Saved presentation (not in fetched list)", library: id });
    }
    return `<fieldset class="pp-assignment"><legend>${escape(a.name || `Tx ${index + 1}`)}</legend>
      <div class="pp-identity"><label>Name<input class="pp-name" value="${escape(a.name)}" required maxlength="80"></label>
      <label>Transmitter device ID<input class="pp-id" value="${escape(a.transmitterId)}" required pattern="[a-fA-F0-9]{12}" maxlength="12" spellcheck="false" placeholder="aabbccddeeff"></label></div>
      <label>Allowed presentations<select class="pp-presentations" multiple size="6">${options.map(p => `<option value="${escape(p.id)}" ${a.presentationIds.some(id => id.toLowerCase() === p.id.toLowerCase()) ? "selected" : ""}>${escape(p.name)} — ${escape(p.library)}</option>`).join("")}</select></label>
      <p class="small">Allow any selected presentation or any name matching a rule below, including newly created presentations. No selections and no rules blocks all presentations.</p>
      <div class="pp-name-rules">${(a.nameRules ?? []).map(nameRuleForm).join("")}</div>
      <button type="button" class="secondary pp-rule-add">Add name rule</button>
      <p class="small">Name contains ignores case. For regex, use (?i) to ignore case and ^…$ to match a full name; omit / delimiters. Lookaround and backreferences are unsupported.</p>
      <button type="button" class="text pp-remove">Remove transmitter</button></fieldset>`;
  }).join("");
  for (const row of root.querySelectorAll<HTMLElement>(".pp-assignment")) {
    row.querySelector<HTMLButtonElement>(".pp-rule-add")!.onclick = () => {
      row.querySelector(".pp-name-rules")!.insertAdjacentHTML("beforeend", nameRuleForm({ kind: "contains", pattern: "" }));
      dirty = true; update();
      row.querySelector<HTMLInputElement>(".pp-name-rule:last-child .pp-rule-pattern")!.focus();
    };
    row.querySelector<HTMLElement>(".pp-name-rules")!.onclick = event => {
      const button = (event.target as HTMLElement).closest(".pp-rule-remove");
      if (button) { button.closest(".pp-name-rule")!.remove(); dirty = true; update(); }
    };
  }
  for (const button of root.querySelectorAll<HTMLButtonElement>(".pp-remove")) button.onclick = () => {
    button.closest(".pp-assignment")!.remove(); dirty = true; update();
  };
}
function message(text: string) { element("message")!.textContent = text; }
function update() {
  const s = state.status;
  const badge = element("connection-state")!;
  badge.textContent = s.connected ? "Connected" : "Disconnected"; badge.classList.toggle("online", s.connected);
  element("room")!.textContent = s.room || "No channel connected";
  element("create")!.textContent = "Listen to channel";
  const arm = element("arm") as HTMLButtonElement;
  arm.textContent = s.armed ? "Pause output" : "Enable output"; arm.classList.toggle("enabled", s.armed);
  element("output-hint")!.textContent = s.armed ? "Output is enabled. Switch to your presentation or control software." : dirty ? "You have unsaved mappings. Save them before enabling output." : "Output is paused. Check output settings, then enable.";
  element("last-event")!.textContent = s.lastEvent || "No clicks received";
  element("pp-last")!.textContent = `Last transmitter: ${s.lastTransmitter || "No identified clicks yet"}`;
  element("received")!.textContent = `${s.received} click${s.received === 1 ? "" : "s"}`;
  element("rtt")!.textContent = s.relayRttMs === null ? "" : `Relay round trip ${s.relayRttMs} ms`;
  (element("output-on-startup") as HTMLInputElement).checked = state.settings.outputOnStartup;
  (element("start-on-boot") as HTMLInputElement).checked = startOnBoot === true;
  for (const element of root.querySelectorAll<HTMLButtonElement | HTMLInputElement | HTMLSelectElement>("button, input, select")) {
    element.disabled = !native || busy || (element.closest("#mappings") !== null && s.armed);
  }
  const proPresenterEnabled = (element("pp-enabled") as HTMLInputElement).checked;
  for (const fieldset of root.querySelectorAll<HTMLFieldSetElement>(".mapping")) fieldset.disabled = proPresenterEnabled || s.armed || busy;
  arm.disabled ||= (!s.connected || dirty) && !s.armed;
  (element("reconnect") as HTMLButtonElement).disabled ||= !s.room || s.armed;
  (element("create") as HTMLButtonElement).disabled ||= s.armed;
  (element("pair") as HTMLButtonElement).disabled ||= !s.room || s.armed;
  (element("start-on-boot") as HTMLInputElement).disabled ||= startOnBoot === null;
}
async function action(command: string, args: Record<string, unknown> = {}, then?: (value: unknown) => void) {
  if (busy || !native) return;
  busy = true; update(); message("");
  try {
    const result = await invoke(command, args); then?.(result); state = await invoke<View>("view");
    if (command === "import_room") {
      (element("pairing") as HTMLTextAreaElement).value = "";
      element("pairing-label")!.hidden = true;
    }
  }
  catch (e) { message(String(e)); }
  finally { busy = false; update(); }
}
async function refresh() {
  if (!native || busy) return;
  try {
    const previousError = state.status.error;
    state = await invoke<View>("view"); update();
    if (state.status.error || previousError !== state.status.error) message(state.status.error);
  }
  catch (e) { message(String(e)); }
}
async function start() {
  if (native) {
    try { state = await invoke<View>("view"); } catch (e) { console.error(e); }
  }
  if (!native) {
    root.innerHTML = `<main><h1>Receiver</h1><p>With UberClicker Pro, run UberClicker Companion on your computer to turn cloud clicks into keyboard presses and OSC messages.</p><p>The desktop app stays connected in the background when you close its window.</p><p><a href="#account">Manage Cloud Channels</a></p><p class="hint">The browser can configure your devices. Keyboard and OSC output require the desktop app.</p></main>`;
    return;
  }
  render();
  await action("autostart_enabled", {}, value => { startOnBoot = Boolean(value); });
  setInterval(() => void refresh(), 500);
}
void start();

}
