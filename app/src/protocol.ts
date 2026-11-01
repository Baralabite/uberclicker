import schemaCheck from "./generated/config-validator";
export const MAX_MESSAGE = 8192;
export const MAX_ACTIONS = 8;
export interface Action {
  type: "lora" | "ble_keyboard" | "ble_midi" | "osc_udp" | "udp" | "tcp" | "cloud";
  enabled: boolean;
  host: string;
  port: number;
  key: number;
  modifiers: number;
  midi: {
    type: "note" | "cc" | "program";
    channel: number;
    number: number;
    value: number;
  };
  osc: string;
  payload: string;
}
export interface Profile {
  name: string;
  previous: Action[];
  next: Action[];
}
export function newAction(type: Action["type"], event: "previous" | "next"): Action {
  const previous = event === "previous";
  return { type, enabled: true, host: type === "cloud" ? "api.uberclicker.com" : "",
    port: type === "cloud" ? 443 : 8000, key: previous ? 80 : 79, modifiers: 0,
    midi: { type: "note", channel: 1, number: previous ? 60 : 62, value: 100 },
    osc: `/${event}`, payload: event };
}
export interface Config {
  schemaVersion: 4;
  revision: number;
  role: "tx" | "rx";
  activeProfile: number;
  profiles: Profile[];
  wifi: { ssid: string; configured: boolean }[];
  radio: "balanced" | "long_range";
  peers: {
    deviceId: string;
    pairId: string;
    name: string;
    adminAuthorized: boolean;
  }[];
  activePeer: number;
}
export function checkedConfig(value: unknown): Config {
  if (!schemaCheck(value)) throw Error("Device returned invalid settings");
  const c = value as Config;
  if (c.activeProfile >= c.profiles.length)
    throw Error("Active profile does not exist");
  for (const p of c.profiles) {
    if (new TextEncoder().encode(p.name).length > 24)
      throw Error("Device names must fit 24 UTF-8 bytes");
    for (const a of [...p.previous, ...p.next]) {
      if (["osc_udp", "udp", "tcp"].includes(a.type) && !a.host)
        throw Error("Network actions need a destination host");
      if (a.type === "cloud" && (a.host !== "api.uberclicker.com" || a.port !== 443))
        throw Error("Cloud root URL must be https://api.uberclicker.com");
      if (new TextEncoder().encode(a.payload).length > 128 || a.payload.includes("\0"))
        throw Error("Action text must fit 128 UTF-8 bytes and contain no null characters");

    }
  }
  for (const network of c.wifi) {
    if (new TextEncoder().encode(network.ssid).length > 32 || network.ssid.includes("\0"))
      throw Error("Network name must fit 32 UTF-8 bytes and contain no null characters");
  }
  if (new Set(c.wifi.map((network) => network.ssid)).size !== c.wifi.length)
    throw Error("Each Wi-Fi network must have a unique name");
  if (
    c.peers.length > (c.role === "rx" ? 1 : 8) ||
    c.activePeer >= (c.peers.length || 1)
  )
    throw Error("Invalid paired-device selection");
  if (
    new Set(c.peers.map((p) => p.deviceId)).size !== c.peers.length ||
    new Set(c.peers.map((p) => p.pairId)).size !== c.peers.length
  )
    throw Error("Duplicate paired device");
  return c;
}
export type Secret =
  | { op: "keep" | "clear" }
  | { op: "replace"; value: string };
export interface Secrets {
  wifi: ({ ssid: string } & Secret)[];
  pairs: ({ deviceId: string } & Secret)[];
}
export interface Status {
  cloud?: { room: string; configured: boolean };
  deviceId: string;
  boardId: string;
  firmwareVersion: string;
  schemaVersion: number;
  protocolVersion: number;
  revision: number;
  setup: boolean;
  editing: boolean;
  claimed: boolean;
  recoveryRequired?: boolean;
  capabilities: {
    ble_config: boolean;
    serial_config?: boolean;
    cloud_output?: boolean;
    fixed_role?: "tx" | "rx";
    lan_config: boolean;
    setup_hotspot: boolean;
    outputs: string[];
    remote_config?: boolean;
    maxReceivers?: number;
    ota_session?: boolean;
    physical_recovery?: boolean;
    broadcast_radio?: boolean;
  };
}
export function parseCloudPairing(code: string): { room: string; token: string } {
  let data: any;
  try { data = JSON.parse(code); } catch { throw Error("Paste the TX pairing code from UberClicker Companion"); }
  if (!data || data.v !== 1 || Object.keys(data).sort().join(",") !== "room,token,v" ||
      typeof data.room !== "string" || !/^[a-f0-9]{32}$/.test(data.room) ||
      typeof data.token !== "string" || !/^[a-f0-9]{64}$/.test(data.token))
    throw Error("Invalid TX pairing code");
  return { room: data.room, token: data.token };
}
export type Reply = {
  v: 2;
  id: number;
  ok: boolean;
  result?: Record<string, unknown>;
  error?: { code: string; message: string };
};
export function encodeFrames(
  message: unknown,
  stream: number,
  attBytes = 20,
): Uint8Array[] {
  const data = new TextEncoder().encode(JSON.stringify(message));
  if (!data.length || data.length > MAX_MESSAGE)
    throw Error("Message exceeds 8192 bytes");
  if (attBytes < 9 || attBytes > 512) throw Error("Invalid ATT size");
  const frames: Uint8Array[] = [];
  for (let offset = 0; offset < data.length; offset += attBytes - 8) {
    const f = new Uint8Array(Math.min(attBytes - 8, data.length - offset) + 8);
    f.set([0x55, 0x43, 1, stream & 255]);
    const v = new DataView(f.buffer);
    v.setUint16(4, offset, true);
    v.setUint16(6, data.length, true);
    f.set(data.subarray(offset, offset + attBytes - 8), 8);
    frames.push(f);
  }
  return frames;
}
export class Reassembler {
  private bytes = new Uint8Array(MAX_MESSAGE);
  private used = 0;
  private total = 0;
  private stream = 0;
  private started = 0;
  private touched = 0;
  reset() {
    this.used = 0;
    this.total = 0;
  }
  feed(
    f: Uint8Array,
    expectedStream?: number,
    now = Date.now(),
  ): unknown | undefined {
    try {
      if (
        this.used &&
        (now - this.touched >= 10000 || now - this.started >= 60000)
      )
        throw Error("Fragment timeout");
      if (
        f.length <= 8 ||
        f.length > 512 ||
        f[0] !== 0x55 ||
        f[1] !== 0x43 ||
        f[2] !== 1
      )
        throw Error("Invalid fragment");
      const v = new DataView(f.buffer, f.byteOffset, f.byteLength),
        off = v.getUint16(4, true),
        total = v.getUint16(6, true);
      if (!total || total > MAX_MESSAGE) throw Error("Message too large");
      if (expectedStream !== undefined && f[3] !== expectedStream)
        throw Error("Unexpected stream");
      if (
        off !== this.used ||
        (this.used && (this.total !== total || this.stream !== f[3])) ||
        off + f.length - 8 > total
      )
        throw Error("Out of order fragment");
      if (!this.used) {
        this.total = total;
        this.stream = f[3];
        this.started = now;
      }
      this.bytes.set(f.subarray(8), off);
      this.used += f.length - 8;
      this.touched = now;
      if (this.used === this.total) {
        const data = new TextDecoder("utf-8", { fatal: true }).decode(
          this.bytes.subarray(0, this.used),
        );
        this.reset();
        return JSON.parse(data);
      }
    } catch (e) {
      this.reset();
      throw e;
    }
  }
}
export interface Transport {
  request(
    op: string,
    args?: Record<string, unknown>,
  ): Promise<Record<string, unknown>>;
  disconnect(): Promise<void>;
}
export interface ApplyJournal {
  load(): Promise<string | undefined>;
  save(transactionId: string): Promise<void>;
  clear(): Promise<void>;
}
export class DeviceSession {
  config?: Config;
  status?: Status;
  session?: string;
  pendingApply?: string;
  leaseExpiresAt?: number;
  constructor(
    public transport: Transport,
    private journal?: ApplyJournal,
  ) {}
  async restorePending() {
    this.pendingApply = await this.journal?.load();
  }
  async resolveApply() {
    if (!this.pendingApply) throw Error("No pending Apply transaction");
    const result = await this.transport.request("result", {
      transactionId: this.pendingApply,
    });
    if (
      result.transactionId !== this.pendingApply ||
      !["applied", "unknown"].includes(String(result.state)) ||
      (result.state === "applied" &&
        (!Number.isInteger(result.revision) || Number(result.revision) < 0))
    )
      throw Error("Invalid Apply receipt; outcome remains unknown");
    if (result.state === "applied") {
      await this.read();
      await this.journal?.clear();
      this.pendingApply = undefined;
    }
    return result;
  }
  async discardPending() {
    await this.journal?.clear();
    this.pendingApply = undefined;
  }

  async refresh() {
    const status = await this.transport.request("status");
    if (
      status.schemaVersion !== 4 ||
      status.protocolVersion !== 2 ||
      typeof status.deviceId !== "string" ||
      !/^[0-9a-f]{12}$/.test(status.deviceId) ||
      typeof status.claimed !== "boolean" ||
      typeof status.setup !== "boolean" ||
      !status.capabilities
    )
      throw Error("Unsupported device settings. Update the app and firmware together");
    this.status = status as unknown as Status;
    return this.status;
  }
  async authorize(token: string) {
    await this.transport.request("authorize", { token });
    await this.read();
  }
  async claim(code: string, recover = false) {
    if (!/^\d{6}$/.test(code))
      throw Error("Enter the six-digit code on your clicker");
    const r = await this.transport.request(recover ? "recover" : "claim", { code });
    if (typeof r.token !== "string" || !/^[0-9a-f]{64}$/.test(r.token))
      throw Error("Invalid claim response");
    return r.token;
  }
  async read() {
    this.config = checkedConfig((await this.transport.request("read")).config);
  }
  async configureCloud(code: string | null) {
    if (this.session || this.pendingApply) throw Error("Apply or cancel the current edit before changing the cloud connection");
    if (!this.status?.capabilities.serial_config || !this.status.capabilities.cloud_output || this.config?.role !== "tx")
      throw Error("Connect a TX with cloud firmware over USB serial");
    const credentials = code === null ? { room: "", token: "" } : parseCloudPairing(code);
    try { await this.transport.request("cloud", credentials); }
    finally { this.config = undefined; }
    await this.refresh(); await this.read();
  }
  async begin() {
    if (this.pendingApply)
      throw Error(
        "Resolve the previous Apply result before starting another edit",
      );
    const r = await this.transport.request("begin");
    if (typeof r.session !== "string") throw Error("Invalid edit response");
    this.session = r.session;
    this.leaseExpiresAt = Date.now() + 120000;
    this.config = checkedConfig(r.config);
    return structuredClone(this.config);
  }
  async apply(config: Config, secrets: Secrets) {
    if (!this.session) throw Error("Start editing first");
    checkedConfig(config);
    try {
      await this.transport.request("validate", {
        session: this.session,
        config,
        secrets,
      });
      this.leaseExpiresAt = Date.now() + 120000;
    } catch (e) {
      if (
        e instanceof Error &&
        ["SESSION_EXPIRED", "SETUP_REQUIRED", "UNAUTHORIZED"].includes(
          e.message,
        )
      )
        this.session = undefined;
      throw e;
    }
    const transactionId = randomHex(16);
    if (this.transport instanceof ReceiverTransport) {
      this.pendingApply = transactionId;
      await this.journal?.save(transactionId);
    }
    try {
      await this.transport.request("apply", {
        session: this.session,
        expectedRevision: this.config!.revision,
        transactionId,
      });
      this.session = undefined;
      this.leaseExpiresAt = undefined;
      await this.read();
      if (this.pendingApply) {
        await this.journal?.clear();
        this.pendingApply = undefined;
      }
    } catch (e) {
      this.session = undefined;
      this.leaseExpiresAt = undefined;
      this.config = undefined;
      throw Error(
        `Apply outcome requires reread. ${e instanceof Error ? e.message : "Connection failed"}`,
      );
    }
  }
  async cancel() {
    try {
      if (this.session)
        await this.transport.request("cancel", { session: this.session });
    } catch (e) {
      this.config = undefined;
      throw e;
    } finally {
      this.session = undefined;
      this.leaseExpiresAt = undefined;
    }
  }
  async openUpdateSession() {
    if (!this.config || !this.status?.capabilities.serial_config || !this.status.capabilities.ota_session)
      throw Error("Connect to the local clicker by USB serial to request updates");
    if (this.session || this.pendingApply)
      throw Error("Resolve or cancel the current edit before opening updates");
    const response = await this.transport.request("updates");
    if (response.requested !== true) throw Error("Update screen request was not accepted");
  }
  lost() {
    this.session = undefined;
    this.leaseExpiresAt = undefined;
    this.config = undefined;
  }
}
export function randomHex(bytes: number) {
  return Array.from(crypto.getRandomValues(new Uint8Array(bytes)), (v) =>
    v.toString(16).padStart(2, "0"),
  ).join("");
}

export function keepPairs(c: Config): Secrets["pairs"] {
  return c.peers.map((p) => ({ deviceId: p.deviceId, op: "keep" }));
}
export class ReceiverTransport implements Transport {
  constructor(
    private transmitter: Transport,
    public readonly receiverId: string,
  ) {
    if (!/^[0-9a-f]{12}$/.test(receiverId)) throw Error("Invalid receiver ID");
  }
  async request(op: string, args: Record<string, unknown> = {}) {
    if (
      ![
        "status",
        "read",
        "begin",
        "validate",
        "apply",
        "cancel",
        "result",
      ].includes(op)
    )
      throw Error("Receiver operation is not permitted");
    const outer = await this.transmitter.request("receiver", {
      deviceId: this.receiverId,
      request: { op, ...args },
    });
    const r = outer.response as Reply | undefined;
    if (
      outer.deviceId !== this.receiverId ||
      !r ||
      r.v !== 2 ||
      typeof r.ok !== "boolean"
    )
      throw Error("Receiver response belongs to another target or protocol");
    if (!r.ok) throw Error(r.error?.code || "Receiver rejected request");
    if (!r.result || typeof r.result !== "object")
      throw Error("Invalid receiver response");
    if (op === "status" && r.result.deviceId !== this.receiverId)
      throw Error("Receiver identity mismatch");
    return r.result;
  }
  async disconnect() {} // The shared Tx connection belongs to its parent session.
}

export function removeReceiver(config: Config, deviceId: string) {
  const cueId = config.peers[config.activePeer]?.deviceId;
  if (!config.peers.some((peer) => peer.deviceId === deviceId))
    throw Error("Receiver is no longer paired");
  if (cueId === deviceId && config.peers.length > 1)
    throw Error(
      "Choose another cue receiver explicitly before removing this one",
    );
  config.peers = config.peers.filter((peer) => peer.deviceId !== deviceId);
  config.activePeer = Math.max(
    0,
    config.peers.findIndex((peer) => peer.deviceId === cueId),
  );
  return config;
}
