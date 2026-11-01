import { test } from "node:test";
import assert from "node:assert/strict";
import { ManualClient } from "../src/manual-client";

class Socket {
  readyState = 1;
  onmessage?: (event: { data: string }) => void;
  onclose?: () => void;
  onerror?: () => void;
  sent: any[] = [];
  send(text: string) { this.sent.push(text === "ping" ? text : JSON.parse(text)); }
  close() { this.readyState = 3; this.onclose?.(); }
  message(data: unknown) { this.onmessage?.({ data: JSON.stringify({ v: 1, ...(data as object) }) }); }
}
function fixture() {
  let now = 0, visible = true;
  const sockets: Socket[] = [], displayed: string[] = [];
  const client = new ManualClient(() => {}, button => { if (!visible) return false; displayed.push(button); return true; }, () => {
    const socket = new Socket(); sockets.push(socket); return socket as unknown as WebSocket;
  }, () => now);
  const sync = (socket: Socket) => socket.message({ type: "sync", nonce: socket.sent.at(-1).nonce, now: 1000000 + now });
  const ready = (socket: Socket) => { socket.message({ type: "ready" }); sync(socket); socket.message({ type: "armed", armed: true }); };
  client.connect("ws://localhost/manual"); ready(sockets[0]);
  return { client, sockets, ready, sync, displayed, advance(ms: number) { now += ms; }, hide() { visible = false; }, click(seq: number, button = "next", age = 0) {
    return { type: "click", id: `${"a".repeat(32)}:${seq}`, button, sentAt: 1000000 + now - age, expiresAt: 1000750 + now - age };
  } };
}
test("Manual displays both directions once and rejects stale, future and hidden cues", () => {
  const f = fixture(), socket = f.sockets[0];
  try {
    socket.message(f.click(1)); socket.message(f.click(1)); socket.message(f.click(2, "previous"));
    socket.message(f.click(3, "next", 750)); socket.message(f.click(4, "next", -101));
    f.hide(); socket.message(f.click(5));
    assert.deepEqual(f.displayed, ["next", "previous"]);
    assert.deepEqual(socket.sent.filter(m => m.type === "ack").map(m => m.status), ["submitted", "submitted", "rejected", "rejected", "rejected"]);
    assert.equal(socket.sent.some(m => m.type === "click"), false);
  } finally { f.client.disconnect(); }
});
test("Manual refreshes an idle clock, drops expired cues, and never replays across reconnect", () => {
  const f = fixture(), socket = f.sockets[0];
  try {
    f.advance(300001); socket.message(f.click(1));
    assert.equal(socket.sent.at(-1).type, "sync"); assert.deepEqual(f.displayed, []);
    f.advance(100); f.sync(socket); assert.deepEqual(f.displayed, ["next"]);
    f.advance(300001); socket.message(f.click(2, "previous", 600));
    f.advance(200); f.sync(socket); assert.deepEqual(f.displayed, ["next"]);
    assert.equal(socket.sent.at(-1).status, "rejected");
    f.advance(300001); socket.message(f.click(3)); f.client.disconnect();
    f.client.connect("ws://localhost/manual"); f.ready(f.sockets[1]);
    f.sync(socket); socket.message(f.click(4));
    assert.deepEqual(f.displayed, ["next"]);
    f.sockets[1].message(f.click(5, "previous")); assert.deepEqual(f.displayed, ["next", "previous"]);
  } finally { f.client.disconnect(); }
});
test("Manual times out slow synchronization and rejects malformed cue data", t => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const f = fixture();
  try {
    f.sockets[0].message({ ...f.click(1), expiresAt: Infinity });
    assert.equal(f.sockets[0].readyState, 3); assert.deepEqual(f.displayed, []);
    f.client.connect("ws://localhost/manual"); f.sockets[1].message({ type: "ready" });
    t.mock.timers.tick(501); assert.equal(f.sockets[1].readyState, 3);
  } finally { f.client.disconnect(); }
});
