import { test } from "node:test";
import assert from "node:assert/strict";
import { destination, storedDestinations, SenderClient } from "../src/sender-client";

const room = "a".repeat(32), token = "b".repeat(64), session = "c".repeat(32);
const target = destination(JSON.stringify({ v: 1, room, token }), "Main stage");
class Socket {
  readyState = 1;
  onmessage?: (event: { data: string }) => void;
  onclose?: (event?: { reason: string }) => void;
  onerror?: () => void;
  sent: any[] = [];
  send(text: string) { this.sent.push(text === "ping" ? text : JSON.parse(text)); }
  close() { this.readyState = 3; this.onclose?.(); }
  message(data: unknown) { this.onmessage?.({ data: typeof data === "string" ? data : JSON.stringify(data) }); }
}
async function fixture() {
  let now = 0;
  const sockets: Socket[] = [];
  const client = new SenderClient(() => {}, (url, protocols) => {
    assert.equal(url, `wss://api.uberclicker.com/v1/rooms/${room}/tx`);
    assert.ok(!url.includes(token));
    assert.deepEqual(protocols, ["uberclicker.v1", `auth.${token}`]);
    const socket = new Socket(); sockets.push(socket); return socket as unknown as WebSocket;
  }, () => now);
  function ready(socket: Socket) {
    socket.message({ v: 1, type: "ready", session, receivers: 1 });
    const sync = socket.sent.at(-1);
    now += 20;
    socket.message({ v: 1, type: "sync", nonce: sync.nonce, now: 1_000_000 + now });
  }
  client.connect(target); ready(sockets[0]);
  await Promise.resolve();
  return { client, sockets, ready, advance(ms: number) { now += ms; } };
}

test("sender authenticates in headers, waits for clock/receiver, and correlates Previous/Next ACKs", async () => {
  const f = await fixture(), socket = f.sockets[0];
  try {
    assert.equal(f.client.state.connected, true);
    for (const [index, button] of ["previous", "next"].entries()) {
      const result = f.client.click(button as "previous" | "next");
      const frame = socket.sent.at(-1);
      assert.equal(frame.button, button); assert.equal(frame.seq, index + 1);
      assert.ok(Number.isSafeInteger(frame.sentAt));
      await assert.rejects(f.client.click("next"), /Wait/);
      socket.message({ v: 1, type: "result", id: `${session}:999`, status: "submitted" });
      assert.equal(f.client.state.busy, true);
      socket.message({ v: 1, type: "result", id: `${session}:${frame.seq}`, status: "submitted" });
      assert.equal(await result, "submitted");
    }
    socket.message({ v: 1, type: "presence", receivers: 0 });
    await assert.rejects(f.client.click("next"), /Enable output/);
    assert.equal(socket.sent.filter(m => m.type === "click").length, 2);
  } finally { f.client.disconnect(); }
});

test("background/disconnect never replays clicks and invalidates a pending clock refresh", async () => {
  const f = await fixture(), socket = f.sockets[0];
  try {
    const sent = f.client.click("previous");
    f.client.disconnect("Background");
    assert.equal(await sent, "unknown");
    f.client.connect(target); f.ready(f.sockets[1]); await Promise.resolve();
    assert.equal(f.sockets[1].sent.filter(m => m.type === "click").length, 0);
    f.advance(300_001);
    const wake = f.client.click("next");
    const rejected = assert.rejects(wake, /disconnected/);
    assert.equal(f.sockets[1].sent.at(-1).type, "sync");
    f.client.disconnect();
    await rejected;
    assert.equal(f.sockets[1].sent.filter(m => m.type === "click").length, 0);
    assert.equal(socket.sent.filter(m => m.type === "click").length, 1);
  } finally { f.client.disconnect(); }
});

test("long-idle click refreshes the clock once and rejects a slow refresh before dispatch", async () => {
  const f = await fixture(), socket = f.sockets[0];
  try {
    f.advance(300_001);
    const click = f.client.click("next");
    const rejected = assert.rejects(click, /disconnected/);
    const sync = socket.sent.at(-1);
    f.advance(600);
    socket.message({ v: 1, type: "sync", nonce: sync.nonce, now: 1_300_621 });
    await rejected;
    assert.equal(socket.sent.filter(m => m.type === "click").length, 0);
    assert.equal(f.client.state.connected, false);
  } finally { f.client.disconnect(); }
});

test("sender idle keepalive sends only auto-response pings, never clock polling", async t => {
  t.mock.timers.enable({ apis: ["setInterval"] });
  const f = await fixture(), socket = f.sockets[0];
  try {
    const syncs = socket.sent.filter(m => m.type === "sync").length;
    for (let i = 0; i < 32; i++) {
      f.advance(20_000); t.mock.timers.tick(20_000); socket.message("pong");
    }
    assert.equal(socket.sent.filter(m => m === "ping").length, 32);
    assert.equal(socket.sent.filter(m => m.type === "sync").length, syncs);
  } finally { f.client.disconnect(); }
});

test("a fresh clock refresh retains the original tap time and missing ACKs remain unknown", async t => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const f = await fixture(), socket = f.sockets[0];
  try {
    f.advance(300_001);
    const click = f.client.click("next");
    const sync = socket.sent.at(-1);
    f.advance(100);
    socket.message({ v: 1, type: "sync", nonce: sync.nonce, now: 1_300_121 });
    await Promise.resolve();
    const frame = socket.sent.at(-1);
    assert.equal(frame.type, "click");
    assert.equal(frame.sentAt, 1_300_071);
    t.mock.timers.tick(651);
    assert.equal(await click, "unknown");
    socket.message({ v: 1, type: "result", id: `${session}:${frame.seq}`, status: "submitted" });
    assert.match(f.client.state.message, /unconfirmed/);
    assert.equal(socket.sent.filter(m => m.type === "click").length, 1);
  } finally { f.client.disconnect(); }
});

test("secure destination records reject malformed or duplicated credentials", () => {
  assert.equal(storedDestinations([target])[0].name, "Main stage");
  assert.throws(() => storedDestinations([target, target]), /Duplicate/);
  assert.throws(() => storedDestinations([{ ...target, token: "bad" }]), /pairing/);
  assert.throws(() => storedDestinations([{ ...target, receiverToken: token }]), /invalid/);
  assert.throws(() => destination(JSON.stringify({ v: 1, room, token }), "x".repeat(41)), /40/);
});

test("account pairing replaces expired codes, clears on link, and never survives disconnect", async () => {
  const f = await fixture(), socket = f.sockets[0];
  try {
    socket.message({ v: 1, type: "pairing", code: "ABCD2345", expiresIn: 600 });
    assert.equal(f.client.state.pairingCode, "ABCD2345");
    assert.equal(f.client.state.linked, false);
    socket.message({ v: 1, type: "pairing", code: "EFGH6789", expiresIn: 600 });
    assert.equal(f.client.state.pairingCode, "EFGH6789");
    socket.message({ v: 1, type: "linked", channelCount: 20 });
    assert.equal(f.client.state.pairingCode, undefined);
    assert.equal(f.client.state.linked, true);
    socket.message({ v: 1, type: "presence", receivers: 160 });
    assert.equal(f.client.state.receivers, 160);
    socket.message({ v: 1, type: "presence", receivers: -1 });
    assert.equal(f.client.state.receivers, 160);
    f.client.disconnect();
    assert.equal(f.client.state.linked, undefined);
    assert.equal(f.client.state.pairingCode, undefined);
  } finally { f.client.disconnect(); }
});

test("account clickers stay in pairing state until linked and connection attempts time out", async t => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const socket = new Socket();
  const client = new SenderClient(() => {}, () => socket as unknown as WebSocket, () => 10);
  try {
    client.connect(target);
    assert.equal(client.state.connecting, true);
    socket.message({ v: 1, type: "ready", account: true, session, receivers: 0 });
    const sync = socket.sent.at(-1);
    socket.message({ v: 1, type: "pairing", code: "ABCD2345" });
    socket.message({ v: 1, type: "sync", nonce: sync.nonce, now: 1_000_000 });
    await Promise.resolve();
    assert.equal(client.state.connecting, false);
    assert.equal(client.state.linked, false);
    await assert.rejects(client.click("next"), /Enable output/);
    socket.message({ v: 1, type: "linked", channelCount: 1 });
    assert.equal(client.state.linked, true);
    assert.equal(client.state.pairingCode, undefined);
    client.connect(target);
    t.mock.timers.tick(10000);
    assert.equal(client.state.connecting, false);
    assert.equal(client.state.connected, false);
    assert.match(client.state.message, /timed out/);
  } finally { client.disconnect(); }
});

test("portal unlink is reported distinctly from ordinary network loss", async () => {
  const f = await fixture();
  try {
    f.sockets[0].onclose?.({ reason: "Clicker unlinked" });
    assert.equal(f.client.state.unlinked, true);
    assert.equal(f.client.state.connected, false);
    f.client.connect(target);
    assert.equal(f.client.state.unlinked, false);
    f.sockets[1].onclose?.({ reason: "" });
    assert.equal(f.client.state.unlinked, false);
  } finally { f.client.disconnect(); }
});

test("unlink during the reconnect handshake enables recovery without sending a click", () => {
  const socket = new Socket();
  const client = new SenderClient(() => {}, () => socket as unknown as WebSocket);
  try {
    client.connect(target);
    socket.onclose?.({ reason: "Clicker unlinked" });
    assert.equal(client.state.unlinked, true);
    assert.equal(client.state.connecting, false);
    assert.equal(client.state.connected, false);
    assert.deepEqual(socket.sent, []);
    client.disconnect("Background");
    assert.equal(client.state.unlinked, true);
  } finally { client.disconnect(); }
});
