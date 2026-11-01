export type Cue = { id: string; button: "next" | "previous"; sentAt: number; expiresAt: number };

export class ManualClient {
  private socket?: WebSocket;
  private heartbeat?: ReturnType<typeof setInterval>;
  private timeout?: ReturnType<typeof setTimeout>;
  private sync?: { nonce: number; start: number };
  private nonce = 0;
  private serverTime = 0;
  private localTime = 0;
  private lastSync = -Infinity;
  private lastMessage = 0;
  private armed = false;
  private pending: Cue[] = [];
  private seen = new Set<string>();

  constructor(
    private changed: (connected: boolean, message: string) => void,
    private display: (button: Cue["button"]) => boolean,
    private socketFactory = (url: string) => new WebSocket(url),
    private now = () => performance.now(),
  ) {}

  connect(url: string) {
    this.disconnect();
    this.changed(false, "Connecting…");
    const socket = this.socket = this.socketFactory(url);
    this.lastMessage = this.now();
    this.timeout = setTimeout(() => this.disconnect("Connection timed out. Retrying…"), 10000);
    socket.onmessage = event => {
      if (socket !== this.socket) return;
      this.lastMessage = this.now();
      if (event.data === "pong") return;
      try {
        if (typeof event.data !== "string" || event.data.length > 2048) throw Error();
        const data = JSON.parse(event.data);
        if (!data || data.v !== 1) throw Error();
        if (data.type === "ready") {
          if (this.sync || this.armed) throw Error();
          this.syncClock();
          this.heartbeat = setInterval(() => {
            if (this.now() - this.lastMessage > 60000) { this.disconnect("Connection lost. Retrying…"); return; }
            if (socket.readyState === 1) socket.send("ping");
          }, 20000);
        } else if (data.type === "sync" && this.sync && data.nonce === this.sync.nonce) {
          const elapsed = this.now() - this.sync.start;
          if (!Number.isSafeInteger(data.now) || elapsed > 500) throw Error();
          this.serverTime = data.now + elapsed / 2;
          this.localTime = this.lastSync = this.now();
          this.sync = undefined; clearTimeout(this.timeout);
          if (!this.armed) this.send({ type: "arm", armed: true });
          for (const cue of this.pending.splice(0)) this.deliver(cue);
        } else if (data.type === "armed" && data.armed === true) {
          this.armed = true; this.changed(true, "Connected · waiting for a cue");
        } else if (data.type === "click" && this.armed) {
          if (typeof data.id !== "string" || !/^[a-f0-9]{32}:\d+$/.test(data.id) ||
              !["next", "previous"].includes(data.button) || !Number.isSafeInteger(data.sentAt) ||
              data.expiresAt !== data.sentAt + 750) throw Error();
          if (this.seen.has(data.id)) return;
          this.seen.add(data.id);
          if (this.seen.size > 128) this.seen.delete(this.seen.values().next().value!);
          if (this.now() - this.lastSync > 300000) {
            if (this.pending.length >= 32) throw Error();
            this.pending.push(data); this.syncClock();
          } else this.deliver(data);
        }
      } catch { this.disconnect("Connection could not be verified. Retrying…"); }
    };
    socket.onerror = () => { if (this.socket === socket) this.disconnect("Unable to connect. Check the network, channel plan and listener limit. Retrying…"); };
    socket.onclose = () => { if (this.socket === socket) this.disconnect("Disconnected. Retrying…"); };
  }

  private send(message: Record<string, unknown>) {
    this.socket?.send(JSON.stringify({ v: 1, ...message }));
  }
  private syncClock() {
    if (this.sync) return;
    this.sync = { nonce: ++this.nonce, start: this.now() };
    clearTimeout(this.timeout);
    this.timeout = setTimeout(() => this.disconnect("Clock check timed out. Retrying…"), 500);
    this.send({ type: "sync", nonce: this.sync.nonce });
  }
  private deliver(cue: Cue) {
    const now = this.serverTime + this.now() - this.localTime;
    const fresh = cue.sentAt <= now + 100 && cue.expiresAt > now;
    const displayed = fresh && this.display(cue.button);
    this.send({ type: "ack", id: cue.id, status: displayed ? "submitted" : "rejected" });
  }
  disconnect(message = "Disconnected.") {
    const socket = this.socket; this.socket = undefined;
    clearInterval(this.heartbeat); clearTimeout(this.timeout);
    this.sync = undefined; this.armed = false; this.pending = []; this.seen.clear(); this.lastSync = -Infinity;
    socket?.close();
    this.changed(false, message);
  }
}
