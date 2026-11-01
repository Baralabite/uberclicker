import { parseCloudPairing } from "./protocol";

export type Destination = { room: string; token: string; name: string };
export type Outcome = "submitted" | "rejected" | "unknown";
export type SenderState = { connected: boolean; connecting?: boolean; busy: boolean; receivers: number; message: string; pairingCode?: string; linked?: boolean; unlinked?: boolean };
const TTL = 750;
const API = "wss://api.uberclicker.com";

export function destination(code: string, name: string): Destination {
  const credentials = parseCloudPairing(code);
  const label = name.trim() || `Channel ${credentials.room.slice(0, 6)}`;
  if (label.length > 40) throw Error("Channel names must fit 40 characters");
  return { ...credentials, name: label };
}

export function storedDestinations(value: unknown): Destination[] {
  if (!Array.isArray(value) || value.length > 32) throw Error("Saved channels are invalid");
  const parsed = value.map(d => {
    if (!d || typeof d.name !== "string" || Object.keys(d).sort().join(",") !== "name,room,token")
      throw Error("Saved channel is invalid");
    return destination(JSON.stringify({ v: 1, room: d.room, token: d.token }), d.name);
  });
  if (new Set(parsed.map(d => d.room)).size !== parsed.length) throw Error("Duplicate saved channel");
  return parsed;
}

export class SenderClient {
  state: SenderState = { connected: false, busy: false, receivers: 0, message: "Choose a channel to connect." };
  private socket?: WebSocket;
  private connectionTimeout?: ReturnType<typeof setTimeout>;
  private heartbeat?: ReturnType<typeof setInterval>;
  private session = "";
  private sequence = 0;
  private nonce = 0;
  private generation = 0;
  private serverTime = 0;
  private localTime = 0;
  private lastSync = -Infinity;
  private lastMessage = 0;
  private syncing?: { nonce: number; start: number; resolve: () => void; reject: (e: Error) => void; timer: ReturnType<typeof setTimeout> };
  private pending?: { id: string; resolve: (outcome: Outcome) => void; timer: ReturnType<typeof setTimeout> };

  constructor(
    private changed: (state: SenderState) => void,
    private socketFactory = (url: string, protocols: string[]) => new WebSocket(url, protocols),
    private now = () => performance.now(),
  ) {}

  private update(message?: string) {
    if (message !== undefined) this.state.message = message;
    this.changed({ ...this.state });
  }

  connect(target: Destination) {
    this.state.unlinked = false;
    this.disconnect();
    this.sequence = 0;
    this.session = "";
    this.lastSync = -Infinity;
    this.lastMessage = this.now();
    this.state.connecting = true;
    this.update("Connecting to channel…");
    // The capability travels in an upgrade header, never in the URL.
    const socket = this.socket = this.socketFactory(`${API}/v1/rooms/${target.room}/tx`, ["uberclicker.v1", `auth.${target.token}`]);
    this.connectionTimeout = setTimeout(() => this.disconnect("Connection timed out. Reconnecting…"), 10000);
    socket.onmessage = event => {
      if (this.socket !== socket) return;
      this.lastMessage = this.now();
      if (event.data === "pong") return;
      try {
        if (typeof event.data !== "string" || event.data.length > 2048) throw Error("Invalid cloud message");
        const data = JSON.parse(event.data);
        if (!data || data.v !== 1) throw Error("Unsupported cloud protocol");
        if (data.type === "ready") {
          if (this.session || typeof data.session !== "string" || !/^[a-f0-9]{32}$/.test(data.session) || !Number.isInteger(data.receivers) || data.receivers < 0 || data.receivers > 160) throw Error("Invalid channel session");
          if (data.account !== true) this.state.linked = true;
          this.session = data.session;
          this.state.receivers = data.receivers;
          void this.syncClock().then(() => {
            if (this.socket !== socket) return;
            clearTimeout(this.connectionTimeout);
            this.state.connecting = false; this.state.connected = true;
            this.update(this.state.pairingCode ? "Enter this code in Cloud Channels to link this clicker." : this.state.receivers ? "Ready to send." : "Choose a channel in your account and enable a Client Companion’s output.");
          }).catch(() => { if (this.socket === socket) this.disconnect("Clock synchronization failed. Reconnect before sending."); });
          this.heartbeat = setInterval(() => {
            if (this.now() - this.lastMessage > 60_000) { this.disconnect("Connection stopped responding. Reconnect before sending."); return; }
            if (socket.readyState === 1) socket.send("ping");
          }, 20_000);
        } else if (data.type === "pairing" && typeof data.code === "string" && /^[A-Z2-9]{8}$/.test(data.code)) {
          this.state.pairingCode = data.code; this.state.linked = false;
          this.update("Enter this code in Cloud Channels to link this clicker.");
        } else if (data.type === "linked") {
          this.state.pairingCode = undefined; this.state.linked = true;
          this.update("Clicker linked. Choose its channels in your account.");
        } else if (data.type === "sync" && this.syncing && data.nonce === this.syncing.nonce) {
          const sync = this.syncing;
          if (!Number.isSafeInteger(data.now) || this.now() - sync.start > 500) throw Error("Cloud clock response was too slow");
          this.serverTime = data.now + (this.now() - sync.start) / 2;
          this.localTime = this.lastSync = this.now();
          this.syncing = undefined;
          clearTimeout(sync.timer);
          sync.resolve();
        } else if (data.type === "presence" && Number.isInteger(data.receivers) && data.receivers >= 0 && data.receivers <= 160) {
          this.state.receivers = data.receivers;
          this.update(data.receivers ? "Ready to send." : "Enable output in a Client Companion.");
        } else if (data.type === "result" && data.id === this.pending?.id && ["submitted", "rejected", "unknown"].includes(data.status)) {
          this.finish(data.status);
        }
      } catch { this.disconnect("Invalid cloud response. Reconnect before sending."); }
    };
    socket.onclose = event => { if (this.socket === socket) { this.state.unlinked = event?.reason === "Clicker unlinked"; this.disconnect("Disconnected. Reconnect before sending."); } };
    socket.onerror = () => { if (this.socket === socket) this.disconnect("Could not connect. Check the channel’s Pro access, pairing code and your network."); };
  }

  private syncClock(): Promise<void> {
    if (!this.socket || this.socket.readyState !== 1 || this.syncing) return Promise.reject(Error("Wait for a connection"));
    const socket = this.socket, nonce = ++this.nonce;
    return new Promise((resolve, reject) => {
      this.syncing = { nonce, start: this.now(), resolve, reject, timer: setTimeout(() => {
        this.syncing = undefined; reject(Error("Cloud clock timed out"));
      }, 500) };
      try { socket.send(JSON.stringify({ v: 1, type: "sync", nonce })); }
      catch { clearTimeout(this.syncing.timer); this.syncing = undefined; reject(Error("Cloud disconnected")); }
    });
  }

  async click(button: "previous" | "next"): Promise<Outcome> {
    if (!this.state.connected || !this.socket || this.socket.readyState !== 1) throw Error("Reconnect before sending");
    if (this.state.busy) throw Error("Wait for the previous click result");
    if (!this.state.receivers) throw Error("Enable output in a Client Companion first");
    const pressed = this.now(), generation = this.generation;
    this.state.busy = true; this.update("Sending…");
    try {
      // No idle clock traffic. Resynchronize only when a real click needs it.
      if (pressed - this.lastSync > 300_000) await this.syncClock();
      if (generation !== this.generation || this.now() - pressed >= TTL || !this.state.connected)
        throw Error("Click expired or connection changed. Nothing was resent.");
      const seq = ++this.sequence;
      if (seq > 0xffffffff) { this.disconnect(); throw Error("Reconnect before sending more clicks"); }
      const sentAt = Math.round(this.serverTime + pressed - this.localTime);
      return await new Promise<Outcome>(resolve => {
        this.pending = { id: `${this.session}:${seq}`, resolve, timer: setTimeout(() => this.finish("unknown"), Math.max(1, TTL - (this.now() - pressed))) };
        try { this.socket!.send(JSON.stringify({ v: 1, type: "click", seq, button, sentAt })); }
        catch { this.finish("unknown"); this.disconnect("Connection lost. Delivery is unconfirmed; check the listeners before clicking again."); }
      });
    } finally { this.state.busy = false; this.update(); }
  }

  private finish(outcome: Outcome) {
    const pending = this.pending;
    if (!pending) return;
    this.pending = undefined;
    clearTimeout(pending.timer);
    this.update(outcome === "submitted" ? "A listener accepted the click." : outcome === "rejected" ? "Click rejected by the relay or a listener. Check each output before trying again." : "Delivery is unconfirmed. Check the listeners before clicking again.");
    pending.resolve(outcome);
  }

  disconnect(message = "Disconnected.") {
    this.generation++;
    clearTimeout(this.connectionTimeout); this.connectionTimeout = undefined;
    this.finish("unknown");
    if (this.heartbeat) clearInterval(this.heartbeat);
    this.heartbeat = undefined;
    if (this.syncing) { clearTimeout(this.syncing.timer); this.syncing.reject(Error("Cloud disconnected")); this.syncing = undefined; }
    const socket = this.socket;
    this.socket = undefined;
    socket?.close();
    this.state.connecting = false; this.state.connected = false; this.state.receivers = 0; this.state.pairingCode = undefined; this.state.linked = undefined;
    this.update(message);
  }
}
