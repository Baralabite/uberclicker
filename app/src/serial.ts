import { MAX_MESSAGE, type Transport, type Reply } from "./protocol";

export class SerialTransport implements Transport {
  private reader: ReadableStreamDefaultReader<Uint8Array> | undefined;
  private writer: WritableStreamDefaultWriter<Uint8Array> | undefined;
  private reading?: Promise<void>;
  private closing?: Promise<void>;
  private closed = false;
  private id = 0;
  private pending?: {
    id: number;
    resolve: (value: Record<string, unknown>) => void;
    reject: (error: Error) => void;
    timer: ReturnType<typeof setTimeout>;
  };
  private constructor(private port: any, private onLost: () => void) {}
  static supported() {
    return typeof navigator !== "undefined" && "serial" in navigator &&
      typeof window !== "undefined" && window.isSecureContext;
  }
  static async discover(onLost: () => void) {
    const port = await (navigator as any).serial.requestPort();
    await port.open({ baudRate: 115200 });
    return SerialTransport.connect(port, onLost);
  }
  // Both Web Serial and the desktop bridge use the same framing and request checks.
  static async connect(port: any, onLost: () => void) {
    const t = new SerialTransport(port, onLost);
    try {
      t.reader = port.readable.getReader();
      t.writer = port.writable.getWriter();
      t.reading = t.read();
      return t;
    } catch (e) {
      await t.disconnect();
      throw e;
    }
  }
  private async read() {
    let buffer = "";
    const decoder = new TextDecoder();
    try {
      while (!this.closed) {
        const { value, done } = await this.reader!.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let at: number;
        while ((at = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, at).trim();
          buffer = buffer.slice(at + 1);
          if (!line.startsWith("UC:") || !this.pending) continue;
          if (new TextEncoder().encode(line.slice(3)).length > MAX_MESSAGE)
            throw Error("Serial response too large");
          let r: Reply;
          try { r = JSON.parse(line.slice(3)); } catch { continue; }
          if (!r || r.v !== 2 || r.id !== this.pending.id) continue;
          const p = this.pending;
          this.pending = undefined;
          clearTimeout(p.timer);
          if (r.ok === true && r.result && typeof r.result === "object" && !Array.isArray(r.result))
            p.resolve(r.result);
          else p.reject(Error(r.ok === false ? r.error?.code || "Device rejected request" : "Invalid serial response"));
        }
        if (buffer.length > MAX_MESSAGE + 3) throw Error("Serial response too large");
      }
    } catch (e) {
      if (this.pending) {
        clearTimeout(this.pending.timer);
        this.pending.reject(e instanceof Error ? e : Error("Serial read failed"));
        this.pending = undefined;
      }
    } finally {
      this.reader!.releaseLock();
      this.reader = undefined;
      // Let this read finish before disconnect waits for its locks to be released.
      if (!this.closed) void this.disconnect().catch(() => {});
    }
  }
  async request(op: string, args: Record<string, unknown> = {}) {
    if (this.closed || this.pending) throw Error("Reconnect or wait for the current request");
    const id = ++this.id;
    const message = JSON.stringify({ ...args, v: 2, id, op });
    const bytes = new TextEncoder().encode(`UC:${message}\n`);
    if (bytes.length > MAX_MESSAGE + 4) throw Error("Message too large");
    return new Promise<Record<string, unknown>>((resolve, reject) => {
      this.pending = {
        id, resolve, reject,
        timer: setTimeout(() => {
          this.pending = undefined;
          reject(Error("Serial timed out. Reread before retrying."));
        }, 15000),
      };
      this.writer!.write(bytes).catch((e: Error) => {
        if (this.pending?.id === id) {
          clearTimeout(this.pending.timer);
          this.pending = undefined;
          reject(e);
        }
      });
    });
  }
  disconnect(): Promise<void> {
    if (this.closing) return this.closing;
    this.closed = true;
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.reject(Error("Serial disconnected"));
      this.pending = undefined;
    }
    this.closing = (async () => {
      try {
        await this.reader?.cancel().catch(() => {});
        await this.reading;
        this.reader?.releaseLock();
        this.reader = undefined;
        this.writer?.releaseLock();
        await this.port.close();
      } finally {
        this.onLost();
      }
    })();
    return this.closing;
  }
}
