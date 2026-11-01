import { test } from "node:test";
import assert from "node:assert/strict";
import { mockIPC, clearMocks } from "@tauri-apps/api/mocks";
import { NativeSerialPort } from "../src/native";
import { SerialTransport } from "../src/serial";

test("desktop USB uses shared framing, survives idle reads and closes once on unplug", async () => {
  const original = Object.getOwnPropertyDescriptor(globalThis, "window");
  Object.defineProperty(globalThis, "window", { configurable: true, value: {} });
  let nextRead: ((bytes: number[]) => void) | undefined;
  let failRead: ((error: Error) => void) | undefined;
  let reads = 0, closed = 0, lost = 0;
  const sent: any[] = [];
  mockIPC(async (command, payload) => {
    const args = payload as Record<string, unknown>;
    if (command === "serial_open") { assert.equal(args?.path, "test-usb"); return 41; }
    assert.equal(args?.session, 41);
    if (command === "serial_read") {
      if (++reads === 1) return []; // A timeout must continue reading, not stall the stream.
      return new Promise<number[]>((resolve, reject) => { nextRead = resolve; failRead = reject; });
    }
    if (command === "serial_write") {
      const frame = new TextDecoder().decode(new Uint8Array(args?.bytes as number[]));
      assert.ok(frame.startsWith("UC:")); assert.ok(frame.endsWith("\n"));
      const request = JSON.parse(frame.slice(3)); sent.push(request);
      const reply = `Boot log\nUC:${JSON.stringify({ v: 2, id: request.id, ok: true, result: { ready: true } })}\n`;
      nextRead!(Array.from(new TextEncoder().encode(reply)));
      return;
    }
    if (command === "serial_close") { closed++; return; }
    assert.fail(`Unexpected command ${command}`);
  });
  try {
    const port = await NativeSerialPort.open("test-usb");
    const transport = await SerialTransport.connect(port, () => lost++);
    await new Promise<void>(resolve => setImmediate(resolve));
    assert.deepEqual(await transport.request("status"), { ready: true });
    assert.equal(sent[0].op, "status");
    await new Promise<void>(resolve => setImmediate(resolve));
    failRead!(Error("USB unplugged"));
    await new Promise<void>(resolve => setImmediate(resolve));
    await Promise.all([transport.disconnect(), transport.disconnect(), port.close()]);
    assert.equal(closed, 1); assert.equal(lost, 1);
    await assert.rejects(transport.request("apply"), /Reconnect/);
  } finally {
    clearMocks();
    if (original) Object.defineProperty(globalThis, "window", original);
    else delete (globalThis as any).window;
  }
});
