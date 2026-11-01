import { invoke, isTauri } from "@tauri-apps/api/core";
import { Capacitor } from "@capacitor/core";

export const desktop = isTauri();
export const mobile = Capacitor.isNativePlatform();
export const nativeInvoke = invoke;
export type PortInfo = { path: string; label: string };

export class NativeSerialPort {
  readonly readable: ReadableStream<Uint8Array>;
  readonly writable: WritableStream<Uint8Array>;
  private closing?: Promise<void>;
  private readClosed = false;

  private constructor(private session: number) {
    this.readable = new ReadableStream({
      pull: async controller => {
        try {
          while (!this.readClosed) {
            const bytes = await invoke<number[]>("serial_read", { session });
            if (bytes.length && !this.readClosed) { controller.enqueue(new Uint8Array(bytes)); return; }
          }
        } catch (error) { if (!this.readClosed) controller.error(error instanceof Error ? error : Error(String(error))); }
      },
      cancel: () => { this.readClosed = true; },
    });
    this.writable = new WritableStream({
      write: bytes => invoke<void>("serial_write", { session, bytes: Array.from(bytes) }),
    });
  }

  static async open(path: string) {
    return new NativeSerialPort(await invoke<number>("serial_open", { path }));
  }

  close() {
    return this.closing ??= invoke<void>("serial_close", { session: this.session });
  }

  setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean; break?: boolean }) {
    return invoke<void>("serial_signals", { session: this.session, dataTerminalReady: signals.dataTerminalReady,
      requestToSend: signals.requestToSend, breakEnabled: signals.break });
  }
}
