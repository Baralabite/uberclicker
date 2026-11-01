import { ManualClient } from "./manual-client";
import "./manual.css";

export function mountManual(root: HTMLDivElement, room: string) {
  document.body.classList.add("manual-mode");
  document.title = "Manual · UberClicker";
  root.innerHTML = `<main class="manual-view" data-cue="idle"><svg class="manual-arrow" viewBox="0 0 64 64" aria-hidden="true"><path d="M12 8 52 32 12 56Z"/></svg><p id="manual-cue" role="status" aria-live="assertive">Standby</p><button type="button" id="manual-sound" aria-pressed="false">Sound off</button><p id="manual-status" role="status">Connecting…</p><p id="manual-feedback" role="status"></p></main>`;
  const signal = root.querySelector<HTMLElement>(".manual-view")!;
  const cue = root.querySelector<HTMLElement>("#manual-cue")!;
  const status = root.querySelector<HTMLElement>("#manual-status")!;
  const feedback = root.querySelector<HTMLElement>("#manual-feedback")!;
  const soundButton = root.querySelector<HTMLButtonElement>("#manual-sound")!;
  let audio: AudioContext | undefined, sound = false;
  let flash: ReturnType<typeof setTimeout> | undefined, retry: ReturnType<typeof setTimeout> | undefined;
  let generation = 0;
  const endpoint = new URL(`/v1/rooms/${room}/manual`, location.origin);
  const socketURL = endpoint.href.replace(/^http/, "ws");
  function reset() {
    clearTimeout(flash); signal.dataset.cue = "idle"; cue.textContent = "Standby";
  }
  function tone(forward: boolean) {
    if (!sound || !audio) return;
    if (audio.state !== "running") {
      sound = false; soundButton.textContent = "Sound off"; soundButton.setAttribute("aria-pressed", "false");
      feedback.textContent = "Audio paused by the browser. Enable sound again to hear cues."; return;
    }
    const oscillator = audio.createOscillator(), gain = audio.createGain(), now = audio.currentTime;
    oscillator.frequency.value = forward ? 880 : 330;
    gain.gain.setValueAtTime(0, now); gain.gain.linearRampToValueAtTime(.18, now + .01);
    gain.gain.exponentialRampToValueAtTime(.001, now + .22);
    oscillator.connect(gain); gain.connect(audio.destination);
    oscillator.start(now); oscillator.stop(now + .24);
    oscillator.onended = () => { oscillator.disconnect(); gain.disconnect(); };
  }
  const client = new ManualClient((ready, message) => {
    status.textContent = ready ? "" : message;
    if (!ready) {
      reset(); clearTimeout(retry);
      if (!document.hidden) retry = setTimeout(() => void connect(), 5000);
    } else { clearTimeout(retry); reset(); }
  }, button => {
    if (document.hidden) return false;
    clearTimeout(flash);
    signal.dataset.cue = button;
    cue.textContent = button === "next" ? "Forward" : "Back";
    tone(button === "next");
    flash = setTimeout(reset, 700);
    return true;
  });
  async function connect() {
    const attempt = ++generation;
    try {
      const response = await fetch(endpoint, { credentials: "omit", cache: "no-store", signal: AbortSignal.timeout(10000) });
      const data = await response.json();
      if (attempt !== generation || document.hidden) return;
      if (!response.ok) {
        status.textContent = data.error || "Channel unavailable.";
        if (response.status === 404 || response.status === 402) return;
        throw Error();
      }
      document.title = `${data.name} · Manual · UberClicker`;
      client.connect(socketURL);
    } catch {
      if (attempt !== generation || document.hidden) return;
      status.textContent = "Unable to connect. Retrying…";
      retry = setTimeout(() => void connect(), 5000);
    }
  }
  soundButton.onclick = async () => {
    if (sound) {
      sound = false; soundButton.textContent = "Sound off"; soundButton.setAttribute("aria-pressed", "false");
      feedback.textContent = ""; return;
    }
    soundButton.disabled = true;
    try {
      audio ??= new AudioContext(); await audio.resume();
      if (audio.state !== "running") throw Error();
      sound = true; soundButton.textContent = "Sound on"; soundButton.setAttribute("aria-pressed", "true");
      feedback.textContent = "";
    } catch { feedback.textContent = "Sound could not start. Try enabling it again."; }
    finally { soundButton.disabled = false; }
  };
  document.addEventListener("visibilitychange", () => {
    generation++; clearTimeout(retry);
    if (document.hidden) client.disconnect("Paused while this view is hidden.");
    else void connect();
  });
  window.addEventListener("pagehide", () => { generation++; client.disconnect(); clearTimeout(retry); });
  window.addEventListener("pageshow", event => { if (event.persisted) void connect(); });
  void connect();
}
