import { NativeSerialPort, nativeInvoke, type PortInfo } from "./native";

// The pinned esptool Transport expects the Web Serial port shape, not its browser chooser.
export class NativeFlashPort {
  private port?: NativeSerialPort;
  constructor(private path: string) {}
  get readable() { return this.port?.readable ?? null; }
  get writable() { return this.port?.writable ?? null; }
  getInfo() { return {}; }
  async open({ baudRate }: { baudRate: number }) {
    if (baudRate !== 115200) throw Error("The native installer requires 115200 baud.");
    if (this.port) throw Error("USB device is already open.");
    this.port = await NativeSerialPort.open(this.path);
  }
  async close() {
    const port = this.port;
    this.port = undefined;
    if (port && !port.readable.locked) await port.readable.cancel().catch(() => {});
    await port?.close();
  }
  setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean; break?: boolean }) {
    if (!this.port) throw Error("USB device is disconnected.");
    return this.port.setSignals(signals);
  }
}

export const nativeInstaller = {
  ports: () => nativeInvoke<PortInfo[]>("serial_ports"),
  port: (path: string) => new NativeFlashPort(path),
  bytes: async (path: string) => new Uint8Array(await nativeInvoke<number[]>("firmware_download", { path })),
};

declare global {
  interface Window { uberclickerInstaller?: typeof nativeInstaller; }
}
