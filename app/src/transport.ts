import { BleClient } from "@capacitor-community/bluetooth-le";
import { Capacitor, CapacitorHttp } from "@capacitor/core";
import {
  SecureStorage,
  KeychainAccess,
} from "@aparajita/capacitor-secure-storage";
import {
  SERVICE,
  WRITE,
  NOTIFY,
  encodeFrames,
  Reassembler,
  type Reply,
  type Transport,
} from "./protocol";
export const native = Capacitor.isNativePlatform();
const memory = new Map<string, string>();
export const credentials = {
  async get(id: string): Promise<string | undefined> {
    if (memory.has(id) || !native) return memory.get(id);
    const v = await SecureStorage.get(id, false);
    return typeof v === "string" ? v : undefined;
  },
  async set(id: string, token: string) {
    memory.set(id, token);
    if (!native) return;
    await SecureStorage.setSynchronize(false);
    await SecureStorage.setDefaultKeychainAccess(
      KeychainAccess.whenUnlockedThisDeviceOnly,
    );
    await SecureStorage.set(id, token, false);
  },
  async remove(id: string) {
    memory.delete(id);
    if (native) await SecureStorage.remove(id, false);
  },
};
export function bleSupported() {
  return native || ("bluetooth" in navigator && window.isSecureContext);
}
export class BleTransport implements Transport {
  private id = 0;
  private assembler = new Reassembler();
  private pending?: {
    id: number;
    stream: number;
    resolve: (v: Record<string, unknown>) => void;
    reject: (e: Error) => void;
    timer: ReturnType<typeof setTimeout>;
  };
  private connected = true;
  private attBytes = 20;
  private constructor(
    private deviceId: string,
    private onLost: () => void,
  ) {}
  static async discover(onLost: () => void) {
    await BleClient.initialize({ androidNeverForLocation: true });
    const d = await BleClient.requestDevice({
      services: [SERVICE],
      optionalServices: [SERVICE],
    });
    const t = new BleTransport(d.deviceId, onLost);
    await BleClient.connect(d.deviceId, () => t.lost());
    try {
      if (Capacitor.getPlatform() === "android")
        await BleClient.createBond(d.deviceId);
      await BleClient.startNotifications(d.deviceId, SERVICE, NOTIFY, (v) =>
        t.receive(v),
      );
      if (native)
        t.attBytes = Math.max(
          20,
          Math.min(512, (await BleClient.getMtu(d.deviceId)) - 3),
        );
      return t;
    } catch (e) {
      await t.disconnect();
      throw e;
    }
  }
  private lost() {
    this.connected = false;
    this.assembler.reset();
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.reject(
        Error("Disconnected. Reconnect and reread settings before retrying."),
      );
      this.pending = undefined;
    }
    this.onLost();
  }
  private receive(v: DataView) {
    if (!this.pending) return;
    try {
      const r = this.assembler.feed(
        new Uint8Array(v.buffer, v.byteOffset, v.byteLength),
        this.pending.stream,
      ) as Reply | undefined;
      if (!r) return;
      const p = this.pending;
      if (r.v !== 2 || r.id !== p.id || typeof r.ok !== "boolean")
        throw Error("Invalid response correlation");
      clearTimeout(p.timer);
      this.pending = undefined;
      if (!r.ok) p.reject(Error(r.error?.code || "Device rejected request"));
      else if (!r.result || typeof r.result !== "object")
        p.reject(Error("Invalid device response"));
      else p.resolve(r.result);
    } catch (e) {
      const p = this.pending;
      if (p) {
        clearTimeout(p.timer);
        this.pending = undefined;
        p.reject(e instanceof Error ? e : Error("Invalid response"));
      }
    }
  }
  async request(op: string, args: Record<string, unknown> = {}) {
    if (!this.connected) throw Error("Reconnect first");
    if (this.pending) throw Error("Wait for the current request");
    const id = ++this.id,
      stream = id & 255,
      frames = encodeFrames({ v: 2, id, op, ...args }, stream, this.attBytes);
    let resolve!: (v: Record<string, unknown>) => void,
      reject!: (e: Error) => void;
    const result = new Promise<Record<string, unknown>>((yes, no) => {
      resolve = yes;
      reject = no;
    });
    this.pending = {
      id,
      stream,
      resolve,
      reject,
      timer: setTimeout(
        () => {
          this.pending = undefined;
          this.assembler.reset();
          reject(
            Error("Response timed out. Reconnect and reread before retrying."),
          );
        },
        op === "receiver" ? 3600000 : 60000,
      ),
    };
    // Attach handler before writes so a disconnect during upload is never unhandled.
    const outcome = result.then(
      (value) => ({ value }),
      (error) => ({ error }),
    );
    try {
      for (const f of frames) {
        if (this.pending?.id !== id) break;
        await BleClient.write(
          this.deviceId,
          SERVICE,
          WRITE,
          new DataView(f.buffer),
        );
      }
    } catch (e) {
      if (this.pending) {
        clearTimeout(this.pending.timer);
        this.pending = undefined;
      }
      reject(e instanceof Error ? e : Error("BLE write failed"));
    }
    const r = await outcome;
    if ("error" in r) throw r.error;
    return r.value;
  }
  async disconnect() {
    if (this.connected) {
      try {
        await BleClient.disconnect(this.deviceId);
      } finally {
        this.lost();
      }
    }
  }
}
export function protectedLanUrl(address: string) {
  const u = new URL(address);
  if (u.protocol !== "https:" || u.username || u.password || u.search || u.hash)
    throw Error("Use an HTTPS address without credentials, query, or fragment");
  u.pathname = "/config/v1";
  return u.href;
}
export class NativeLanTransport implements Transport {
  private id = 0;
  private busy = false;
  constructor(private address: string) {
    if (!native) throw Error("LAN access requires the native app");
    this.address = protectedLanUrl(address);
  }
  async request(op: string, args: Record<string, unknown> = {}) {
    if (this.busy) throw Error("Wait for current request");
    this.busy = true;
    try {
      const id = ++this.id,
        r = await CapacitorHttp.post({
          url: this.address,
          headers: { "Content-Type": "application/json" },
          data: { v: 2, id, op, ...args },
          connectTimeout: 10000,
          readTimeout: 10000,
        });
      if (r.status !== 200) throw Error(`LAN request failed (${r.status})`);
      const d = (
        typeof r.data === "string" ? JSON.parse(r.data) : r.data
      ) as Reply;
      if (d.v !== 2 || d.id !== id || !d.ok || !d.result)
        throw Error(d.error?.code || "Invalid LAN response");
      return d.result;
    } finally {
      this.busy = false;
    }
  }
  async disconnect() {}
}
