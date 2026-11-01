import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ts from "typescript";

// Exercise the actual Durable Object with an in-memory Cloudflare socket/storage adapter.
class Socket {
  messages = []; attachment; closed;
  send(text) { this.messages.push(JSON.parse(text)); }
  close(code) { this.closed = code; }
  serializeAttachment(value) { this.attachment = structuredClone(value); }
  deserializeAttachment() { return structuredClone(this.attachment); }
}
let session = 0;
const module = { exports: {} };
const source = ts.transpileModule(readFileSync(new URL("../src/index.ts", import.meta.url), "utf8"), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
}).outputText;
new Function("require", "module", "exports", "WebSocketPair", "WebSocketRequestResponsePair", "Response", "setTimeout", "clearTimeout", source)(
  name => {
    if (name === "cloudflare:workers") return { DurableObject: class { constructor(ctx, env) { this.ctx = ctx; this.env = env; } } };
    if (name === "./accounts") return { hash: async token => token, hex: () => (++session).toString(16).padStart(32, "0"), json: (_, status) => ({ status }) };
    if (name === "./billing" || name === "./billing-account" || name === "./clickers") return {};
    throw Error(name);
  }, module, module.exports,
  class { constructor() { this[0] = new Socket(); this[1] = new Socket(); } }, class {},
  class { constructor(_, init) { Object.assign(this, init); } }, () => 0, () => {},
);
async function room() {
  const sockets = [], stored = new Map();
  const ctx = {
    waitUntil(promise) { promise.catch(error => { throw error; }); }, setWebSocketAutoResponse() {}, blockConcurrencyWhile: fn => fn(),
    getWebSockets: role => sockets.filter(ws => !ws.closed && (!role || ws.role === role)),
    acceptWebSocket: (ws, [role]) => { ws.role = role; sockets.push(ws); },
    storage: { get: async key => stored.get(key), put: async values => { for (const [key, value] of Object.entries(values)) stored.set(key, value); }, setAlarm: async () => {}, deleteAlarm: async () => {} },
  };
  const room = new module.exports.ClickRoom(ctx, {});
  await room.initialize({ tx: "a".repeat(64), client: "b".repeat(64) }, Date.now() + 60000);
  const connect = async role => {
    const response = await room.fetch(new Request(`https://relay.test/v1/rooms/${"0".repeat(32)}/${role}`, { headers: { Authorization: `Bearer ${(role === "tx" ? "a" : "b").repeat(64)}` } }));
    return { response, ws: sockets.at(-1) };
  };
  const send = (ws, data) => room.webSocketMessage(ws, JSON.stringify({ v: 1, ...data }));
  return { connect, send };
}

test("multiple transmitters retain their stable identity and acknowledgements are isolated", async () => {
  const { connect, send } = await room();
  const { ws: receiver } = await connect("client");
  send(receiver, { type: "arm", armed: true });
  const { ws: tx1 } = await connect("tx"), { ws: tx2, response } = await connect("tx");
  assert.equal(response.status, 101);
  for (const [tx, transmitterId] of [[tx1, "aabbccddeeff"], [tx2, "112233445566"]]) {
    send(tx, { type: "click", seq: 1, button: "next", sentAt: Date.now(), transmitterId });
    const click = receiver.messages.at(-1);
    assert.equal(click.type, "click"); assert.equal(click.transmitterId, transmitterId);
    assert.equal(click.id, `${tx.attachment.session}:1`);
    send(receiver, { type: "ack", id: click.id, status: "submitted" });
    assert.equal(tx.messages.at(-1).id, click.id);
  }
  assert.equal(tx1.messages.filter(m => m.type === "result").length, 1);
  assert.equal(tx2.messages.filter(m => m.type === "result").length, 1);
});

test("legacy clicks remain compatible; malformed identities fail closed; room limits stay bounded", async () => {
  const { connect, send } = await room();
  const { ws: receiver } = await connect("client"); send(receiver, { type: "arm", armed: true });
  const { ws: legacy } = await connect("tx");
  send(legacy, { type: "click", seq: 1, button: "previous", sentAt: Date.now() });
  assert.equal(receiver.messages.at(-1).type, "click");
  assert.equal(receiver.messages.at(-1).transmitterId, undefined);
  const count = receiver.messages.length;
  send(legacy, { type: "click", seq: 2, button: "next", sentAt: Date.now(), transmitterId: "invalid" });
  assert.equal(legacy.closed, 1008); assert.equal(receiver.messages.length, count);
  for (let i = 0; i < 8; i++) assert.equal((await connect("tx")).response.status, 101);
  assert.equal((await connect("tx")).response.status, 409);
});
