import { createClickerInvite, redeemClickerInvite } from "./clicker-invites";
import { DurableObject } from "cloudflare:workers";
import type { Env } from "./index";
import { hash, hex, json } from "./accounts";

type Clicker = { id: string; token_hash: string; name: string; user_id: string | null; pairing_expires: number; revoked: number };
type Member = { session: string; seq: number; window: number; count: number; pending: string[]; lastSeen?: number; persistedAt?: number };
const validId = /^[a-f0-9]{32}$/, validToken = /^[a-f0-9]{64}$/;
const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

export async function pairing(env: Env, id: string): Promise<string> {
  const bytes = crypto.getRandomValues(new Uint8Array(8));
  const code = Array.from(bytes, b => alphabet[b % alphabet.length]).join("");
  await env.DB.prepare("UPDATE clickers SET pairing_hash = ?, pairing_expires = ? WHERE id = ? AND user_id IS NULL AND revoked = 0")
    .bind(await hash(code), Date.now() + 600000, id).run();
  return code;
}

export async function registerClicker(request: Request, env: Env): Promise<Response> {
  if (!(await env.CREATE_LIMIT.limit({ key: `clicker:${request.headers.get("CF-Connecting-IP") ?? "local"}` })).success)
    return json({ error: "Please wait a minute before linking another clicker." }, 429);
  const raw = await request.text();
  if (raw.length > 1024) return json({ error: "Request too large" }, 413);
  let input: { name?: unknown; id?: unknown; token?: unknown; invite?: unknown };
  try { input = JSON.parse(raw); } catch { return json({ error: "Invalid request" }, 400); }
  if (!input || typeof input.name !== "string" || !input.name.trim() || input.name.length > 80)
    return json({ error: "Clicker name must be 1–80 characters" }, 400);
  const id = input.id ?? hex(16), token = input.token ?? hex(32);
  if (typeof id !== "string" || !validId.test(id) || typeof token !== "string" || !validToken.test(token))
    return json({ error: "Invalid clicker credentials" }, 400);
  if (await env.DB.prepare("SELECT id FROM rooms WHERE id = ?").bind(id).first()) return json({ error: "Clicker unavailable" }, 403);
  if (input.invite !== undefined) return redeemClickerInvite(env, input.invite, id, token, input.name.trim());
  const digest = await hash(token);
  const existing = await env.DB.prepare("SELECT * FROM clickers WHERE id = ?").bind(id).first<Clicker>();
  if (existing && (existing.revoked || existing.token_hash !== digest)) return json({ error: "Clicker unavailable" }, 403);
  if (!existing) await env.DB.prepare("INSERT INTO clickers (id, token_hash, name, created_at, last_seen) VALUES (?, ?, ?, ?, ?)").bind(id, digest, input.name.trim(), Date.now(), Date.now()).run();
  return json({ v: 1, room: id, token }, existing ? 200 : 201);
}

export async function clickerAccount(request: Request, env: Env, user: string): Promise<Response | undefined> {
  const path = new URL(request.url).pathname;
  const invitation = /^\/api\/rooms\/([a-f0-9]{32})\/clicker-link$/.exec(path);
  if (invitation && request.method === "POST") return createClickerInvite(env, user, invitation[1]);
  if (path === "/api/clickers/link" && request.method === "POST") {
    const { code } = await request.json<{ code: unknown }>();
    if (typeof code !== "string" || !/^[A-Z2-9]{8}$/.test(code.replace(/[\s-]/g, "").toUpperCase())) return json({ error: "Enter the eight-character code shown on your clicker." }, 400);
    const claimed = await env.DB.prepare("UPDATE clickers SET user_id = ?, pairing_hash = NULL, pairing_expires = 0 WHERE pairing_hash = ? AND pairing_expires > ? AND user_id IS NULL AND revoked = 0 RETURNING id")
      .bind(user, await hash(code.replace(/[\s-]/g, "").toUpperCase()), Date.now()).first<{ id: string }>();
    if (!claimed) return json({ error: "Code expired or already used. Request a new code on the clicker." }, 400);
    await env.CLICKERS.get(env.CLICKERS.idFromName(claimed.id)).refresh(claimed.id);
    return json({ ok: true });
  }
  if (path === "/api/clickers/routes" && request.method === "POST") {
    const { routes } = await request.json<{ routes: { id: string; channels: string[] }[] }>();
    if (!Array.isArray(routes) || routes.length > 100 || routes.some(r => !r || typeof r.id !== "string" || !validId.test(r.id) || !Array.isArray(r.channels) || r.channels.length > 20 || r.channels.some(c => typeof c !== "string" || !validId.test(c)) || new Set(r.channels).size !== r.channels.length) || new Set(routes.map(r => r.id)).size !== routes.length) return json({ error: "Invalid routing" }, 400);
    const owned = await env.DB.prepare("SELECT id FROM clickers WHERE user_id = ? AND revoked = 0").bind(user).all<{ id: string }>();
    const allowed = await env.DB.prepare("SELECT id FROM rooms WHERE user_id = ? AND revoked = 0").bind(user).all<{ id: string }>();
    if (routes.some(r => !owned.results.some(d => d.id === r.id) || r.channels.some(c => !allowed.results.some(a => a.id === c)))) return json({ error: "Choose clickers and channels from your account" }, 403);
    const statements = routes.flatMap(r => [env.DB.prepare("DELETE FROM clicker_routes WHERE clicker_id = ?").bind(r.id), ...r.channels.map(c => env.DB.prepare("INSERT INTO clicker_routes (clicker_id, room_id) VALUES (?, ?)").bind(r.id, c))]);
    if (statements.length) await env.DB.batch(statements);
    await Promise.all(routes.map(r => env.CLICKERS.get(env.CLICKERS.idFromName(r.id)).refresh(r.id)));
    return json({ ok: true });
  }
  const route = /^\/api\/clickers\/([a-f0-9]{32})\/(unlink)$/.exec(path);
  if (!route || request.method !== "POST") return;
  const owned = await env.DB.prepare("SELECT id FROM clickers WHERE id = ? AND user_id = ? AND revoked = 0").bind(route[1], user).first();
  if (!owned) return json({ error: "Clicker not found" }, 404);
  await env.DB.batch([
    env.DB.prepare("DELETE FROM clicker_routes WHERE clicker_id = ?").bind(route[1]),
    env.DB.prepare("UPDATE clickers SET revoked = 1, pairing_hash = NULL WHERE id = ? AND user_id = ?").bind(route[1], user),
  ]);
  await env.CLICKERS.get(env.CLICKERS.idFromName(route[1])).refresh(route[1]);
  return json({ ok: true });
}

export class CloudClicker extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
  }
  private send(ws: WebSocket, data: object) { try { ws.send(JSON.stringify({ v: 1, ...data })); } catch { try { ws.close(1011, "Reconnect"); } catch {} } }
  async fetch(request: Request): Promise<Response> {
    const id = new URL(request.url).pathname.split("/")[3];
    const token = request.headers.get("Authorization")?.replace(/^Bearer /, "") ?? "";
    const clicker = await this.env.DB.prepare("SELECT * FROM clickers WHERE id = ?").bind(id).first<Clicker>();
    if (!clicker || !validToken.test(token) || clicker.token_hash !== await hash(token)) return json({ error: "Unauthorized" }, 401);
    if (clicker.revoked) {
      // Browsers hide failed-upgrade response bodies; return the same unlink signal as a live connection.
      const pair = new WebSocketPair();
      pair[1].accept();
      pair[1].close(1008, "Clicker unlinked");
      return new Response(null, { status: 101, webSocket: pair[0], headers: request.headers.get("Sec-WebSocket-Protocol")?.includes("uberclicker.v1") ? { "Sec-WebSocket-Protocol": "uberclicker.v1" } : {} });
    }
    if (this.ctx.getWebSockets().length) return json({ error: "Clicker already connected" }, 409);
    await this.ctx.storage.put("id", id);
    await this.env.DB.prepare("UPDATE clickers SET last_seen = MAX(last_seen, ?) WHERE id = ?").bind(Date.now(), id).run();
    const pair = new WebSocketPair(), ws = pair[1];
    this.ctx.acceptWebSocket(ws);
    const member: Member = { session: hex(16), seq: 0, window: Date.now(), count: 0, pending: [], lastSeen: Date.now(), persistedAt: Date.now() };
    ws.serializeAttachment(member);
    this.send(ws, { type: "ready", account: true, session: member.session, now: Date.now(), receivers: 0 });
    await this.refresh(id);
    return new Response(null, { status: 101, webSocket: pair[0], headers: request.headers.get("Sec-WebSocket-Protocol")?.includes("uberclicker.v1") ? { "Sec-WebSocket-Protocol": "uberclicker.v1" } : {} });
  }
  async lastSeen(): Promise<number> {
    return Math.max(0, ...this.ctx.getWebSockets().map(ws => Math.max((ws.deserializeAttachment() as Member).lastSeen ?? 0, this.ctx.getWebSocketAutoResponseTimestamp(ws)?.getTime() ?? 0)));
  }
  private async persistSeen(at: number) {
    const id = await this.ctx.storage.get<string>("id");
    if (id) await this.env.DB.prepare("UPDATE clickers SET last_seen = MAX(last_seen, ?) WHERE id = ?").bind(at, id).run();
  }
  private activity(ws: WebSocket, member: Member, now: number) {
    member.lastSeen = now;
    if (now - (member.persistedAt ?? 0) >= 60000) { member.persistedAt = now; this.ctx.waitUntil(this.persistSeen(now)); }
    ws.serializeAttachment(member);
  }
  async refresh(id: string): Promise<void> {
    const sockets = this.ctx.getWebSockets();
    if (!sockets.length) return;
    const clicker = await this.env.DB.prepare("SELECT * FROM clickers WHERE id = ?").bind(id).first<Clicker>();
    if (!clicker || clicker.revoked) { for (const ws of sockets) ws.close(1008, "Clicker unlinked"); return; }
    if (!clicker.user_id) {
      const code = await pairing(this.env, id);
      for (const ws of sockets) this.send(ws, { type: "pairing", code, expiresIn: 600 });
      await this.ctx.storage.setAlarm(Date.now() + 600000);
      return;
    }
    await this.ctx.storage.deleteAlarm();
    const routes = await this.routes(id);
    const counts = await Promise.all(routes.map(r => this.env.ROOMS.get(this.env.ROOMS.idFromName(r.id)).listenerCount()));
    for (const ws of sockets) {
      this.send(ws, { type: "linked", channelCount: routes.length });
      this.send(ws, { type: "presence", receivers: counts.reduce((a, b) => a + b, 0) });
    }
  }
  private async routes(id: string) {
    const result = await this.env.DB.prepare("SELECT rooms.id, rooms.name FROM clicker_routes JOIN clickers ON clickers.id = clicker_routes.clicker_id JOIN rooms ON rooms.id = clicker_routes.room_id WHERE clickers.id = ? AND clickers.revoked = 0 AND rooms.revoked = 0 AND rooms.user_id = clickers.user_id AND rooms.pro_until > ? AND rooms.status IN ('active', 'trialing')").bind(id, Date.now()).all<{ id: string; name: string }>();
    return result.results;
  }
  async alarm(): Promise<void> { const id = await this.ctx.storage.get<string>("id"); if (id) await this.refresh(id); }
  async webSocketMessage(ws: WebSocket, raw: string | ArrayBuffer): Promise<void> {
    const member = ws.deserializeAttachment() as Member;
    if (typeof raw !== "string" || raw.length > 512) { ws.close(1008, "Invalid message"); return; }
    const now = Date.now();
    if (now - member.window >= 1000) { member.window = now; member.count = 0; }
    if (++member.count > 40) { ws.close(1008, "Rate exceeded"); return; }
    ws.serializeAttachment(member);
    let m: Record<string, unknown>;
    try { m = JSON.parse(raw); } catch { ws.close(1008, "Invalid JSON"); return; }
    if (!m || m.v !== 1) { ws.close(1008, "Invalid protocol"); return; }
    if (m.type === "sync" && Number.isSafeInteger(m.nonce)) { this.activity(ws, member, now); this.send(ws, { type: "sync", nonce: m.nonce, now }); return; }
    if (m.type !== "click" || !Number.isSafeInteger(m.seq) || Number(m.seq) <= member.seq || Number(m.seq) > 0xffffffff || !["previous", "next"].includes(String(m.button)) || !Number.isSafeInteger(m.sentAt) || (m.transmitterId !== undefined && (typeof m.transmitterId !== "string" || !/^[a-f0-9]{12}$/.test(m.transmitterId)))) { ws.close(1008, "Invalid click"); return; }
    member.seq = Number(m.seq);
    this.activity(ws, member, now);
    const id = `${member.session}:${m.seq}`;
    member.pending = [...member.pending.slice(-31), id]; ws.serializeAttachment(member);
    if (Number(m.sentAt) > now + 100 || now - Number(m.sentAt) >= 750) { await this.result(id, "rejected"); return; }
    const deviceId = await this.ctx.storage.get<string>("id");
    const routes = deviceId ? await this.routes(deviceId) : [];
    const deliveries = await Promise.all(routes.map(r => this.env.ROOMS.get(this.env.ROOMS.idFromName(r.id)).publishClicker(deviceId!, { id, button: String(m.button), sentAt: Number(m.sentAt), ...(m.transmitterId ? { transmitterId: String(m.transmitterId) } : {}) })));
    if (!deliveries.some(Boolean)) await this.result(id, "rejected");
  }
  async result(id: string, status: string): Promise<void> {
    for (const ws of this.ctx.getWebSockets()) {
      const member = ws.deserializeAttachment() as Member;
      if (member.pending.includes(id)) {
        member.pending = member.pending.filter(value => value !== id); ws.serializeAttachment(member);
        this.send(ws, { type: "result", id, status });
      }
    }
  }
  webSocketClose(ws: WebSocket) { this.ctx.waitUntil(this.persistSeen(Date.now())); try { ws.close(1000); } catch {} }
  webSocketError(ws: WebSocket) { this.webSocketClose(ws); }
}
