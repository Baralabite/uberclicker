import { before, after, test } from "node:test";
import assert from "node:assert/strict";
import { createHmac } from "node:crypto";
import { spawn, execFileSync } from "node:child_process";
import { mkdtemp, rm, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createServer } from "node:net";
import WebSocket from "ws";

let processHandle, temporary;
let port, api, cookie;
const sockets = [];
before(async () => {
  const probe = createServer(); await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  port = probe.address().port; await new Promise(resolve => probe.close(resolve)); api = `http://127.0.0.1:${port}`;
  temporary = await mkdtemp(join(tmpdir(), "uberclicker-relay-"));
  execFileSync(process.execPath, ["node_modules/wrangler/bin/wrangler.js", "d1", "migrations", "apply", "uberclicker-accounts", "--local", "--persist-to", temporary], { stdio: "pipe" });
  const config = JSON.parse(await readFile("wrangler.jsonc", "utf8"));
  config.main = join(process.cwd(), "src/index.ts"); config.assets.directory = join(process.cwd(), "../app/dist");
  config.routes = []; config.vars.APP_ORIGIN = api; config.vars.EMAIL_ENABLED = "true";
  config.vars.BETTER_AUTH_SECRET = "local-test-only-secret-not-for-production-123456";
  config.vars.STRIPE_WEBHOOK_SECRET = "whsec_localtest"; config.vars.STRIPE_SECRET_KEY = "sk_test_local";
  config.ratelimits.forEach(r => { r.simple.limit = 100; });
  await writeFile(join(temporary, "wrangler.json"), JSON.stringify(config));
  processHandle = spawn(process.execPath, ["node_modules/wrangler/bin/wrangler.js", "dev", "--local", "--port", String(port), "--show-interactive-dev-session=false", "--persist-to", temporary, "--config", join(temporary, "wrangler.json")], { env: { ...process.env, WRANGLER_SEND_METRICS: "false" }, stdio: ["ignore", "pipe", "pipe"] });
  let output = "";
  processHandle.stdout.on("data", b => { output += b; }); processHandle.stderr.on("data", b => { output += b; });
  const start = Date.now();
  while (Date.now() - start < 20000) {
    try { if ((await (await fetch(`${api}/health`)).json()).service === "uberclicker-cloud") break; } catch {}
    if (processHandle.exitCode !== null) throw Error(output);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  if (Date.now() - start >= 20000) throw Error(`Local relay did not start: ${output}`);
  const signup = await post("/api/auth/sign-up/email", { name: "Relay Test", email: "relay@example.com", password: "Local-test-password-123!" });
  assert.equal(signup.status, 200, await signup.text());
  sql("UPDATE user SET emailVerified = 1 WHERE email = 'relay@example.com'");
  const login = await post("/api/auth/sign-in/email", { email: "relay@example.com", password: "Local-test-password-123!" });
  assert.equal(login.status, 200, await login.clone().text());
  cookie = login.headers.getSetCookie().map(s => s.split(";")[0]).join("; ");
  assert.ok(cookie.includes("session_token"));
});
after(async () => {
  for (const ws of sockets) ws.terminate();
  if (processHandle && processHandle.exitCode === null) {
    const stopped = new Promise(resolve => processHandle.once("exit", resolve));
    processHandle.kill("SIGTERM"); await stopped;
  }
  if (temporary) await rm(temporary, { recursive: true, force: true });
});
function sql(command) {
  return execFileSync(process.execPath, ["node_modules/wrangler/bin/wrangler.js", "d1", "execute", "uberclicker-accounts", "--local", "--persist-to", temporary, "--command", command], { stdio: "pipe" });
}
async function post(path, body, session = cookie, origin = api) {
  return fetch(`${api}${path}`, { method: "POST", headers: { "Content-Type": "application/json", Origin: origin, ...(session ? { Cookie: session } : {}) }, body: JSON.stringify(body) });
}
async function room(active = true) {
  const response = await post("/v1/rooms", { name: "Test destination" }); assert.equal(response.status, 201, await response.clone().text());
  const result = await response.json();
  if (active) sql(`UPDATE rooms SET status = 'trialing', pro_until = ${Date.now() + 3600000} WHERE id = '${result.room}'`);
  return result;
}
async function connect(c, role, token = role === "tx" ? c.txToken : c.clientToken) {
  const ws = new WebSocket(`${api.replace("http", "ws")}/v1/rooms/${c.room}/${role}`, { headers: role === "manual" ? {} : { Authorization: `Bearer ${token}` } });
  sockets.push(ws);
  const messages = [], waiters = [];
  ws.on("message", bytes => {
    const text = String(bytes), m = text === "pong" ? { type: "pong" } : JSON.parse(text);
    const at = waiters.findIndex(w => w.type === m.type);
    if (at === -1) messages.push(m); else { const w = waiters.splice(at, 1)[0]; clearTimeout(w.timer); w.resolve(m); }
  });
  const next = (type, timeout = 2000) => {
    const at = messages.findIndex(m => m.type === type);
    if (at !== -1) return Promise.resolve(messages.splice(at, 1)[0]);
    return new Promise((resolve, reject) => {
      const w = { type, resolve, timer: setTimeout(() => { const at = waiters.indexOf(w); if (at >= 0) waiters.splice(at, 1); reject(Error(`Missing ${type}`)); }, timeout) };
      waiters.push(w);
    });
  };
  await new Promise((resolve, reject) => { ws.once("open", resolve); ws.once("error", reject); });
  return { ws, next, send: m => ws.send(JSON.stringify({ v: 1, ...m })), ready: await next("ready") };
}

async function expectUnlinked(device) {
  const ws = new WebSocket(`${api.replace("http", "ws")}/v1/rooms/${device.room}/tx`, ["uberclicker.v1", `auth.${device.token}`]);
  sockets.push(ws);
  const messages = [];
  ws.on("message", bytes => messages.push(String(bytes)));
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { ws.terminate(); reject(Error("Missing unlink notification")); }, 2000);
    ws.once("error", error => { clearTimeout(timer); reject(error); });
    ws.once("close", (code, reason) => {
      clearTimeout(timer);
      try {
        assert.equal(ws.protocol, "uberclicker.v1");
        assert.equal(code, 1008); assert.equal(String(reason), "Clicker unlinked");
        assert.deepEqual(messages, []); // Revoked credentials never receive a usable session.
        resolve();
      } catch (error) { reject(error); }
    });
  });
}

test("authenticated rooms relay fresh clicks once, acknowledge output, and never replay", async () => {
  const c = await room(); assert.match(c.txToken, /^[a-f0-9]{64}$/); assert.notEqual(c.txToken, c.clientToken);
  const receiver = await connect(c, "client"), tx = await connect(c, "tx");
  // An unarmed receiver never executes/receives a click.
  tx.send({ type: "click", seq: 1, button: "next", sentAt: Date.now() });
  assert.equal((await tx.next("result")).status, "rejected");
  receiver.send({ type: "arm", armed: true }); await receiver.next("armed");
  const stamp = Date.now(); tx.send({ type: "click", seq: 2, button: "next", sentAt: stamp });
  const click = await receiver.next("click");
  assert.equal(click.button, "next"); assert.equal(click.expiresAt, stamp + 750); assert.equal(click.id, `${tx.ready.session}:2`);
  receiver.send({ type: "ack", id: click.id, status: "submitted" });
  assert.equal((await tx.next("result")).status, "submitted");
  tx.send({ type: "click", seq: 3, button: "previous", sentAt: Date.now() - 1000 });
  assert.equal((await tx.next("result")).status, "rejected");
  tx.send({ type: "click", seq: 4, button: "next", sentAt: Date.now() + 5000 });
  assert.equal((await tx.next("result")).status, "rejected");
  receiver.ws.close(); await new Promise(resolve => receiver.ws.once("close", resolve));
  tx.send({ type: "click", seq: 5, button: "next", sentAt: Date.now() });
  assert.equal((await tx.next("result")).status, "rejected");
  const reconnected = await connect(c, "client"); reconnected.send({ type: "arm", armed: true }); await reconnected.next("armed");
  await assert.rejects(reconnected.next("click", 150), /Missing click/);
  tx.send({ type: "click", seq: 6, button: "previous", sentAt: Date.now() });
  assert.equal((await reconnected.next("click")).button, "previous");
  // Duplicate sequence numbers terminate the publisher instead of repeating a cue.
  const closed = new Promise(resolve => tx.ws.once("close", code => resolve(code)));
  tx.send({ type: "click", seq: 6, button: "previous", sentAt: Date.now() });
  assert.equal(await closed, 1008);
});

test("publisher credentials cannot receive clicks; rooms stay isolated", async () => {
  const a = await room(), b = await room();
  async function denied(id, role, token) {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(`${api.replace("http", "ws")}/v1/rooms/${id}/${role}`, { headers: token ? { Authorization: `Bearer ${token}` } : {} });
      sockets.push(ws); ws.on("error", () => {});
      const timer = setTimeout(() => { ws.terminate(); reject(Error("Authentication check timed out")); }, 2000);
      ws.once("unexpected-response", (_, response) => { clearTimeout(timer); response.resume(); ws.terminate(); resolve(response.statusCode); });
      ws.once("open", () => { clearTimeout(timer); ws.terminate(); reject(Error("Unauthorized connection accepted")); });
    });
  }
  for (const [id, role, token] of [[a.room, "client", a.txToken], [a.room, "tx", a.clientToken], [b.room, "tx", a.txToken]]) {
    assert.equal(await denied(id, role, token), 401);
  }
  assert.equal(await denied(a.room, "tx"), 401);
});

test("clock sync and heartbeat work after an idle hibernation interval", { timeout: 20000 }, async () => {
  const c = await room(), tx = await connect(c, "tx");
  await new Promise(resolve => setTimeout(resolve, 12000));
  tx.ws.send("ping"); await tx.next("pong");
  tx.send({ type: "sync", nonce: 42 });
  const sync = await tx.next("sync"); assert.equal(sync.nonce, 42); assert.ok(Math.abs(sync.now - Date.now()) < 1000);
});


test("accounts reject anonymous and cross-origin creation; each destination needs its own entitlement", async () => {
  assert.equal((await post("/v1/rooms", { name: "Anonymous" }, "")).status, 401);
  assert.equal((await post("/v1/rooms", { name: "CSRF" }, cookie, "https://evil.example")).status, 403);
  const response = await fetch(`${api}/api/account`, { headers: { Cookie: cookie } });
  assert.equal(response.status, 200);
  const account = await response.json(); assert.equal(account.user.email, "relay@example.com");
  assert.equal(account.billing.hasCustomer, false);
  assert.ok(account.rooms.length > 0); assert.equal(JSON.stringify(account).includes("txToken"), false);
  const c = await room(false);
  await assert.rejects(connect(c, "tx"), /402/);
  assert.equal((await post("/api/billing/webhook", {})).status, 400);
  const payload = JSON.stringify({ id: "evt_local", type: "invoice.paid", data: { object: { customer: "cus_unrelated" } } });
  const timestamp = Math.floor(Date.now() / 1000);
  const signature = createHmac("sha256", "whsec_localtest").update(`${timestamp}.${payload}`).digest("hex");
  const signed = await fetch(`${api}/api/billing/webhook`, { method: "POST", body: payload, headers: { "stripe-signature": `t=${timestamp},v1=${signature}` } });
  assert.equal(signed.status, 200, await signed.text());
});

test("browser protocol auth supports multiple channel listeners and revocation", async () => {
  const c = await room();
  const receiver = await connect(c, "client"), tx = await connect(c, "tx");
  receiver.send({ type: "arm", armed: true }); await receiver.next("armed");
  const second = await connect(c, "client");
  second.send({ type: "arm", armed: true }); await second.next("armed");
  const browser = new WebSocket(`${api.replace("http", "ws")}/v1/rooms/${c.room}/tx`, ["uberclicker.v1", `auth.${c.txToken}`]);
  sockets.push(browser);
  const ready = await new Promise((resolve, reject) => { browser.once("error", reject); browser.once("message", data => resolve(JSON.parse(String(data)))); });
  assert.equal(browser.protocol, "uberclicker.v1"); assert.notEqual(ready.session, tx.ready.session);
  browser.send(JSON.stringify({ v: 1, type: "click", seq: 1, button: "next", sentAt: Date.now() }));
  assert.equal((await receiver.next("click")).id, `${ready.session}:1`);
  assert.equal((await second.next("click")).id, `${ready.session}:1`);
  const listenerClosed = new Promise(resolve => second.ws.once("close", resolve));
  const closed = new Promise(resolve => browser.once("close", resolve));
  assert.equal((await post(`/api/rooms/${c.room}/revoke`, {})).status, 200);
  assert.equal(await closed, 1008);
  assert.equal(await listenerClosed, 1008);
  await assert.rejects(connect(c, "tx"), /402/);
});


test("channels broadcast to enabled listeners, isolate acknowledgements, and cap connections", async () => {
  const c = await room(), tx = await connect(c, "tx"), listeners = [];
  for (let i = 0; i < 8; i++) listeners.push(await connect(c, "client"));
  await assert.rejects(connect(c, "client"), /409/);
  await assert.rejects(connect(c, "manual"), /409/);
  for (const listener of listeners.slice(0, 2)) {
    listener.send({ type: "arm", armed: true }); await listener.next("armed");
  }
  tx.send({ type: "click", seq: 1, button: "next", sentAt: Date.now() });
  const first = await listeners[0].next("click"), second = await listeners[1].next("click");
  assert.equal(first.id, second.id);
  listeners[0].send({ type: "ack", id: first.id, status: "submitted" });
  assert.equal((await tx.next("result")).status, "submitted");
  listeners[1].send({ type: "ack", id: second.id, status: "rejected" });
  assert.equal((await tx.next("result")).status, "rejected");
  listeners[0].send({ type: "ack", id: first.id, status: "submitted" });
  await assert.rejects(tx.next("result", 150), /Missing result/);
  await assert.rejects(listeners[2].next("click", 150), /Missing click/);
  listeners[0].send({ type: "arm", armed: false }); await listeners[0].next("armed");
  tx.send({ type: "click", seq: 2, button: "previous", sentAt: Date.now() });
  assert.equal((await listeners[1].next("click")).button, "previous");
  await assert.rejects(listeners[0].next("click", 150), /Missing click/);
});


test("one-time device linking supports one or many account-owned channels without replay", { timeout: 20000 }, async () => {
  const first = await room(), second = await room();
  const registered = await post("/v1/clickers", { name: "M5 test" }, "");
  assert.equal(registered.status, 201);
  const device = await registered.json();
  const clicker = await connect(device, "tx", device.token);
  const pairing = await clicker.next("pairing");
  assert.match(pairing.code, /^[A-Z2-9]{8}$/);
  assert.equal(JSON.stringify(pairing).includes(device.token), false);
  assert.equal((await post("/api/clickers/link", { code: pairing.code }, "")).status, 401);
  assert.equal((await post("/api/clickers/link", { code: pairing.code })).status, 200);
  await clicker.next("linked");
  assert.equal((await post("/api/clickers/link", { code: pairing.code })).status, 400);
  const account = await (await fetch(`${api}/api/account`, { headers: { Cookie: cookie } })).json();
  assert.ok(account.devices.some(d => d.id === device.room));
  assert.equal(JSON.stringify(account).includes(device.token), false);
  assert.equal((await post("/api/clickers/routes", { routes: [{ id: device.room, channels: [first.room, second.room] }] })).status, 200);
  assert.equal((await post("/api/clickers/routes", { routes: [{ id: device.room, channels: ["f".repeat(32)] }] })).status, 403);
  assert.equal((await post("/api/clickers/routes", { routes: [{ id: device.room, channels: [first.room, first.room] }] })).status, 400);
  assert.equal((await post("/v1/clickers", { name: "Collision", id: first.room, token: "a".repeat(64) })).status, 403);
  const a = await connect(first, "manual"), b = await connect(second, "client");
  for (const receiver of [a,b]) { receiver.send({ type: "arm", armed: true }); await receiver.next("armed"); }
  clicker.send({ type: "click", seq: 1, button: "next", sentAt: Date.now(), transmitterId: "aabbccddeeff" });
  const ca = await a.next("click"), cb = await b.next("click");
  assert.equal(ca.id, cb.id); assert.equal(ca.transmitterId, "aabbccddeeff");
  a.send({ type: "ack", id: ca.id, status: "submitted" });
  assert.equal((await clicker.next("result")).status, "submitted");
  assert.equal((await post("/api/clickers/routes", { routes: [{ id: device.room, channels: [second.room] }] })).status, 200);
  clicker.send({ type: "click", seq: 2, button: "previous", sentAt: Date.now() });
  assert.equal((await b.next("click")).button, "previous");
  await assert.rejects(a.next("click", 150), /Missing click/);
  clicker.send({ type: "click", seq: 3, button: "next", sentAt: Date.now()-1000 });
  assert.equal((await clicker.next("result")).status, "rejected");
  await assert.rejects(b.next("click", 150), /Missing click/);
  const stopped = new Promise(resolve => clicker.ws.once("close", resolve));
  assert.equal((await post(`/api/clickers/${device.room}/unlink`, {})).status, 200);
  assert.equal(await stopped, 1008);
  await expectUnlinked(device);
  await assert.rejects(connect(device, "tx", "f".repeat(64)), /401/);
});

test("a phone unlinked while offline receives the recovery signal on its next connection", async () => {
  const device = await (await post("/v1/clickers", { name: "Offline phone" }, "")).json();
  const phone = await connect(device, "tx", device.token);
  const pairing = await phone.next("pairing");
  assert.equal((await post("/api/clickers/link", { code: pairing.code })).status, 200);
  await phone.next("linked");
  const closed = new Promise(resolve => phone.ws.once("close", resolve));
  phone.ws.close(); await closed;
  assert.equal((await post(`/api/clickers/${device.room}/unlink`, {})).status, 200);
  await expectUnlinked(device);
  assert.equal((await post("/v1/clickers", { name: "Offline phone", id: device.room, token: device.token }, "")).status, 403);
  const replacement = await (await post("/v1/clickers", { name: "Offline phone" }, "")).json();
  assert.notEqual(replacement.room, device.room);
  const reconnected = await connect(replacement, "tx", replacement.token);
  assert.match((await reconnected.next("pairing")).code, /^[A-Z2-9]{8}$/);
});

test("expired linking codes and other accounts cannot claim or route a clicker", async () => {
  const response = await post("/v1/clickers", { name: "Expired link" }, "");
  const device = await response.json();
  const client = await connect(device, "tx", device.token), pairing = await client.next("pairing");
  sql(`UPDATE clickers SET pairing_expires = 0 WHERE id = '${device.room}'`);
  assert.equal((await post("/api/clickers/link", { code: pairing.code })).status, 400);
  assert.equal((await post("/api/clickers/routes", { routes: [{ id: device.room, channels: [] }] })).status, 403);
  const wrong = await post("/v1/clickers", { name: "Replacement", id: device.room, token: "f".repeat(64) }, "");
  assert.equal(wrong.status, 403);
});

test("signed channel links authenticate, redeem once, and recover a lost response for the same clicker", { timeout: 20000 }, async () => {
  const channel = await room();
  assert.equal((await post(`/api/rooms/${channel.room}/clicker-link`, {}, "")).status, 401);
  assert.equal((await post(`/api/rooms/${"0".repeat(32)}/clicker-link`, {})).status, 404);
  const link = await (await post(`/api/rooms/${channel.room}/clicker-link`, {})).json();
  assert.ok(link.url.startsWith(`${api}/#send?invite=`));
  const invite = new URLSearchParams(new URL(link.url).hash.split("?")[1]).get("invite");
  const device = { name: "Invited phone", id: crypto.randomUUID().replaceAll("-", ""), token: "e".repeat(64), invite };
  assert.equal((await post("/v1/clickers", { ...device, invite: invite.slice(0, -1) + (invite.endsWith("a") ? "b" : "a") }, "")).status, 400);
  const competitor = { ...device, id: crypto.randomUUID().replaceAll("-", ""), token: "f".repeat(64) };
  const responses = await Promise.all([post("/v1/clickers", device, ""), post("/v1/clickers", competitor, "")]);
  assert.deepEqual(responses.map(r => r.status).sort(), [201, 400]);
  const winner = responses[0].status === 201 ? device : competitor;
  const registered = await (await post("/v1/clickers", winner, "")).json();
  assert.equal(registered.room, winner.id);
  assert.equal(registered.token, winner.token);
  assert.equal((await post("/v1/clickers", { ...winner, token: "a".repeat(64) }, "")).status, 400);
  const clicker = await connect(registered, "tx", registered.token);
  assert.equal((await clicker.next("linked")).channelCount, 1);
  await assert.rejects(clicker.next("pairing", 100), /Missing pairing/);
  sql(`UPDATE clickers SET last_seen = 1 WHERE id = '${winner.id}'`);
  const pingAt = Date.now(); clicker.ws.send("ping"); await clicker.next("pong");
  const account = await (await fetch(`${api}/api/account`, { headers: { Cookie: cookie } })).json();
  const linked = account.devices.find(d => d.id === winner.id);
  assert.deepEqual(linked.channels, [channel.room]);
  assert.ok(linked.last_seen >= pingAt);
  assert.deepEqual(account.devices.map(d => d.last_seen), account.devices.map(d => d.last_seen).sort((a,b) => b-a));
  assert.equal(JSON.stringify(account).includes(winner.token), false);
});

test("signed links reject expiration and revoked channels; channel renaming checks ownership", async () => {
  const channel = await room();
  assert.equal((await post(`/api/rooms/${channel.room}/rename`, { name: "Renamed channel" })).status, 200);
  assert.equal((await post(`/api/rooms/${channel.room}/rename`, { name: " " })).status, 400);
  assert.equal((await post(`/api/rooms/${"0".repeat(32)}/rename`, { name: "Not mine" })).status, 404);
  const make = async () => {
    const { url } = await (await post(`/api/rooms/${channel.room}/clicker-link`, {})).json();
    return new URLSearchParams(new URL(url).hash.split("?")[1]).get("invite");
  };
  const invite = await make(), nonce = invite.split(".")[0], expiry = Date.now()-1000;
  sql(`UPDATE clicker_invites SET expires = ${expiry} WHERE id = '${nonce}'`);
  const signature = createHmac("sha256", "local-test-only-secret-not-for-production-123456").update(`clicker-invite-v1:${nonce}.${expiry}`).digest("hex");
  assert.equal((await post("/v1/clickers", { name: "Expired", invite: `${nonce}.${expiry}.${signature}` }, "")).status, 400);
  const revoked = await make();
  sql(`UPDATE rooms SET revoked = 1 WHERE id = '${channel.room}'`);
  assert.equal((await post("/v1/clickers", { name: "Revoked", invite: revoked }, "")).status, 400);
  assert.equal((await post(`/api/rooms/${channel.room}/clicker-link`, {})).status, 404);
});


test("public Manual views receive cues without login but cannot publish; entitlement and revocation apply", async () => {
  const c = await room(false);
  const endpoint = `/v1/rooms/${c.room}/manual`;
  assert.equal((await fetch(`${api}${endpoint}`)).status, 402);
  await assert.rejects(connect(c, "manual"), /402/);
  sql(`UPDATE rooms SET status = 'trialing', pro_until = ${Date.now() + 3600000} WHERE id = '${c.room}'`);
  const page = await fetch(`${api}/manual/${c.room}`);
  assert.equal(page.status, 200);
  const html = await page.text(); assert.match(html, /<div id="app">/); assert.match(html, /<base href="\/">/);
  const info = await fetch(`${api}${endpoint}`);
  assert.deepEqual(await info.json(), { name: "Test destination" });
  assert.equal(info.headers.get("Cache-Control"), "no-store");
  const manual = await connect(c, "manual"), tx = await connect(c, "tx");
  manual.send({ type: "arm", armed: true }); await manual.next("armed");
  for (const [index, button] of ["next", "previous"].entries()) {
    tx.send({ type: "click", seq: index + 1, button, sentAt: Date.now() });
    const cue = await manual.next("click"); assert.equal(cue.button, button);
    manual.send({ type: "ack", id: cue.id, status: "submitted" });
    assert.equal((await tx.next("result")).status, "submitted");
  }
  const closed = new Promise(resolve => manual.ws.once("close", resolve));
  manual.send({ type: "click", seq: 1, button: "next", sentAt: Date.now() });
  assert.equal(await closed, 1008);
  const reconnected = await connect(c, "manual"); reconnected.send({ type: "arm", armed: true }); await reconnected.next("armed");
  await assert.rejects(reconnected.next("click", 150), /Missing click/);
  const revoked = new Promise(resolve => reconnected.ws.once("close", resolve));
  assert.equal((await post(`/api/rooms/${c.room}/revoke`, {})).status, 200);
  assert.equal(await revoked, 1008);
  assert.equal((await fetch(`${api}${endpoint}`)).status, 404);
  await assert.rejects(connect(c, "manual"), /404/);
});
