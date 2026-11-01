import { Capacitor, CapacitorHttp } from "@capacitor/core";
import type { Reply, Transport } from "./protocol";
export const native = Capacitor.isNativePlatform();
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
