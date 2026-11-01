import { DurableObject } from "cloudflare:workers";

import { accounts, hex, hash, json } from "./accounts";
import { registerClicker, type CloudClicker } from "./clickers";
export { CloudClicker } from "./clickers";
import type { BillingAccount } from "./billing-account";
export { BillingAccount } from "./billing-account";
import { hasPro } from "./billing";

export interface Env {
  ASSETS: Fetcher;
  ROOMS: DurableObjectNamespace<ClickRoom>;
  CLICKERS: DurableObjectNamespace<CloudClicker>;
  CREATE_LIMIT: RateLimit;
  AUTH_LIMIT: RateLimit;
  DB: D1Database;
  BILLING: DurableObjectNamespace<BillingAccount>;
  EMAIL: SendEmail;
  EMAIL_ENABLED: string;
  APP_ORIGIN: string;
  BETTER_AUTH_SECRET: string;
  GOOGLE_CLIENT_ID?: string;
  GOOGLE_CLIENT_SECRET?: string;
  STRIPE_SECRET_KEY: string;
  STRIPE_WEBHOOK_SECRET: string;
  STRIPE_MONTHLY_PRICE: string;
  STRIPE_YEARLY_PRICE: string;
}
type Role = "tx" | "client" | "manual";
type Member = {
  role: Role; session: string; seq: number; armed: boolean;
  window: number; count: number; pending: string[]; warmUntil: number; routed?: Record<string, string>;
};
type Credentials = { tx: string; client: string };
const TTL = 750;
const tokenPattern = /^[a-f0-9]{64}$/;
const roomPattern = /^[a-f0-9]{32}$/;

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname === "/setup") return Response.redirect(`${url.origin}/setup/`, 302);
    if (url.pathname.startsWith("/setup/") && (request.method === "GET" || request.method === "HEAD")) {
      url.pathname = url.pathname.slice(6);
      return env.ASSETS.fetch(new Request(url, request));
    }
    if (url.pathname === "/v1/clickers" && ["POST", "OPTIONS"].includes(request.method)) {
      const headers = { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "POST", "Access-Control-Allow-Headers": "Content-Type" };
      if (request.method === "OPTIONS") return new Response(null, { status: 204, headers });
      let response: Response;
      try { response = await registerClicker(request, env); }
      catch { response = json({ error: "Could not link clicker. Try again." }, 503); }
      for (const [key, value] of Object.entries(headers)) response.headers.set(key, value);
      return response;
    }
    if (url.pathname === "/health" && request.method === "GET") return json({ service: "uberclicker-cloud", protocol: 1 });
    if (url.pathname.startsWith("/api/") || url.pathname === "/v1/rooms") {
      try { return await accounts(request, env); }
      catch (e) {
        // Provider failures must not leak credentials or personal information.
        console.error("Account request failed", e instanceof Error ? e.name : "Error");
        return json({ error: "This request could not be completed. Please try again." }, 503);
      }
    }
    if (url.origin === env.APP_ORIGIN && /^\/manual\/[a-f0-9]{32}\/?$/.test(url.pathname) && ["GET", "HEAD"].includes(request.method)) {
      url.pathname = "/";
      // Native builds use relative assets; public deep links resolve them from the web root.
      const page = await env.ASSETS.fetch(new Request(url, request));
      return new HTMLRewriter().on("head", { element(head) { head.prepend('<base href="/">', { html: true }); } }).transform(page);
    }
    if (url.origin === env.APP_ORIGIN && ["GET", "HEAD"].includes(request.method) && !url.pathname.startsWith("/v1/")) return env.ASSETS.fetch(request);
    const route = /^\/v1\/rooms\/([a-f0-9]{32})\/(tx|client|manual)$/.exec(url.pathname);
    if (!route || request.method !== "GET" || !roomPattern.test(route[1])) return json({ error: "Not found" }, 404);
    const upgrade = request.headers.get("Upgrade")?.toLowerCase() === "websocket";
    if (!upgrade && route[2] !== "manual") return json({ error: "WebSocket required" }, 426);
    const protocols = request.headers.get("Sec-WebSocket-Protocol")?.split(",").map(s => s.trim()) ?? [];
    const browserToken = protocols.find(p => /^auth\.[a-f0-9]{64}$/.test(p))?.slice(5);
    if (route[2] === "tx" && protocols.includes("uberclicker.v1") && browserToken) {
      const headers = new Headers(request.headers); headers.set("Authorization", `Bearer ${browserToken}`);
      request = new Request(request, { headers });
    }
    const auth = request.headers.get("Authorization") ?? "";
    if (route[2] !== "manual" && !/^Bearer [a-f0-9]{64}$/.test(auth)) return json({ error: "Unauthorized" }, 401);
    if (route[2] === "tx" && await env.DB.prepare("SELECT id FROM clickers WHERE id = ?").bind(route[1]).first()) {
      return env.CLICKERS.get(env.CLICKERS.idFromName(route[1])).fetch(request);
    }
    // Existing anonymous rooms have no owner and cannot bypass the Pro entitlement.
    const billing = await env.DB.prepare("SELECT name, status, pro_until FROM rooms WHERE id = ? AND revoked = 0").bind(route[1]).first<{ name: string; status: string; pro_until: number }>();
    if (route[2] === "manual" && !billing) return json({ error: "This channel is unavailable. Check the link with its owner." }, 404);
    if (!hasPro(billing)) return json({ error: "An active UberClicker Pro subscription is required" }, 402);
    if (!upgrade) return json({ name: billing!.name });
    const stub = env.ROOMS.get(env.ROOMS.idFromName(route[1]));
    await stub.refreshEntitlement(route[1]);
    return stub.fetch(request);
  },
} satisfies ExportedHandler<Env>;

export class ClickRoom extends DurableObject<Env> {
  private proUntil = 0;
  private warm?: ReturnType<typeof setTimeout>;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
    ctx.blockConcurrencyWhile(async () => { this.proUntil = await ctx.storage.get<number>("proUntil") ?? 0; });
    this.updateWarmth();
  }

  async initialize(credentials: Credentials, proUntil: number): Promise<void> {
    await this.ctx.blockConcurrencyWhile(async () => {
      if (await this.ctx.storage.get("credentials")) throw new Error("Room already exists");
      await this.ctx.storage.put({ credentials, proUntil });
      this.proUntil = proUntil;
      if (proUntil > Date.now()) await this.ctx.storage.setAlarm(proUntil);
    });
  }

  async refreshEntitlement(room: string): Promise<void> {
    await this.ctx.storage.put("roomId", room);
    await this.ctx.blockConcurrencyWhile(async () => {
      const current = await this.env.DB.prepare("SELECT status, pro_until FROM rooms WHERE id = ? AND revoked = 0").bind(room).first<{ status: string; pro_until: number }>();
      await this.setEntitlement(hasPro(current) ? current!.pro_until : 0);
    });
  }

  async setEntitlement(until: number): Promise<void> {
    this.proUntil = until;
    await this.ctx.storage.put("proUntil", until);
    if (until <= Date.now()) {
      await this.ctx.storage.deleteAlarm();
      for (const ws of this.ctx.getWebSockets()) try { ws.close(1008, "UberClicker Pro access ended"); } catch {}
    } else await this.ctx.storage.setAlarm(until);
  }
  async alarm(): Promise<void> {
    if (this.proUntil <= Date.now()) await this.setEntitlement(0);
  }

  async fetch(request: Request): Promise<Response> {
    if (this.proUntil <= Date.now()) return json({ error: "UberClicker Pro required" }, 402);
    const role = new URL(request.url).pathname.split("/").pop() as Role;
    if (role !== "tx" && role !== "client" && role !== "manual") return json({ error: "Not found" }, 404);
    const credentials = await this.ctx.storage.get<Credentials>("credentials");
    const token = request.headers.get("Authorization")?.replace(/^Bearer /, "") ?? "";
    if (!credentials || (role !== "manual" && (!tokenPattern.test(token) || await hash(token) !== credentials[role]))) return json({ error: "Unauthorized" }, 401);
    if (this.proUntil <= Date.now()) return json({ error: "UberClicker Pro required" }, 402);
    const connections = role === "tx" ? this.ctx.getWebSockets("tx") : [...this.ctx.getWebSockets("client"), ...this.ctx.getWebSockets("manual")];
    if (connections.length >= 8) return json({ error: "Channel connection limit reached" }, 409);
    const pair = new WebSocketPair(), client = pair[0], server = pair[1];
    this.ctx.acceptWebSocket(server, [role]);
    const member: Member = { role, session: hex(16), seq: 0, armed: false, window: Date.now(), count: 0, pending: [], warmUntil: 0 };
    server.serializeAttachment(member);
    this.send(server, { v: 1, type: "ready", session: member.session, now: Date.now(), receivers: this.receivers().length });
    this.presence();
    const protocols = request.headers.get("Sec-WebSocket-Protocol")?.split(",").map(s => s.trim()) ?? [];
    return new Response(null, { status: 101, webSocket: client, ...(protocols.includes("uberclicker.v1") ? { headers: { "Sec-WebSocket-Protocol": "uberclicker.v1" } } : {}) });
  }

  private send(ws: WebSocket, message: unknown): boolean {
    try { ws.send(JSON.stringify(message)); return true; }
    catch { try { ws.close(1011, "Connection unavailable"); } catch {} return false; }
  }
  listenerCount(): number { return this.proUntil > Date.now() ? this.receivers().length : 0; }
  async publishClicker(device: string, click: { id: string; button: string; sentAt: number; transmitterId?: string }): Promise<boolean> {
    if (this.proUntil <= Date.now() || Date.now() - click.sentAt >= TTL) return false;
    let sent = false;
    for (const ws of this.receivers()) {
      const member = ws.deserializeAttachment() as Member;
      member.pending = [...member.pending.slice(-31), click.id];
      member.routed = Object.fromEntries(Object.entries(member.routed ?? {}).filter(([id]) => member.pending.includes(id)));
      member.routed[click.id] = device; ws.serializeAttachment(member);
      sent = this.send(ws, { v: 1, type: "click", ...click, expiresAt: click.sentAt + TTL }) || sent;
    }
    return sent;
  }
  private receivers(): WebSocket[] {
    return [...this.ctx.getWebSockets("client"), ...this.ctx.getWebSockets("manual")].filter(ws => ws.deserializeAttachment()?.armed);
  }
  private updateWarmth(): void {
    const until = Math.max(0, ...this.ctx.getWebSockets("tx").map(ws => (ws.deserializeAttachment() as Member).warmUntil));
    if (this.warm) clearTimeout(this.warm);
    this.warm = undefined;
    // A timer keeps the room awake for ten minutes after a real click, never an arm or heartbeat.
    if (until > Date.now()) this.warm = setTimeout(() => { this.warm = undefined; }, until - Date.now());
  }
  private presence(): void {
    const receivers = this.receivers().length;
    for (const ws of this.ctx.getWebSockets("tx")) this.send(ws, { v: 1, type: "presence", receivers });
    this.updateWarmth();
    this.ctx.waitUntil((async () => {
      const name = await this.ctx.storage.get<string>("roomId");
      if (!name) return;
      const routes = await this.env.DB.prepare("SELECT clicker_id FROM clicker_routes WHERE room_id = ?").bind(name).all<{ clicker_id: string }>();
      await Promise.all(routes.results.map(r => this.env.CLICKERS.get(this.env.CLICKERS.idFromName(r.clicker_id)).refresh(r.clicker_id)));
    })());
  }

  webSocketMessage(ws: WebSocket, message: string | ArrayBuffer): void {
    if (this.proUntil <= Date.now()) { ws.close(1008, "UberClicker Pro access ended"); return; }
    const member = ws.deserializeAttachment() as Member | undefined;
    if (!member || typeof message !== "string" || message.length > 512) { ws.close(1008, "Invalid message"); return; }
    const now = Date.now();
    if (now - member.window >= 1000) { member.window = now; member.count = 0; }
    if (++member.count > 40) { ws.close(1008, "Rate exceeded"); return; }
    ws.serializeAttachment(member);
    let m: Record<string, unknown>;
    try { m = JSON.parse(message); } catch { ws.close(1008, "Invalid JSON"); return; }
    if (!m || typeof m !== "object" || Array.isArray(m) || m.v !== 1) { ws.close(1008, "Invalid protocol"); return; }
    if (m.type === "sync" && Number.isSafeInteger(m.nonce)) {
      this.send(ws, { v: 1, type: "sync", nonce: m.nonce, now }); return;
    }
    if (member.role !== "tx" && m.type === "arm" && typeof m.armed === "boolean") {
      member.armed = m.armed; member.pending = []; member.routed = {};
      ws.serializeAttachment(member); this.presence();
      this.send(ws, { v: 1, type: "armed", armed: member.armed }); return;
    }
    if (member.role !== "tx" && m.type === "ack" && typeof m.id === "string" &&
        ["submitted", "rejected", "unknown"].includes(String(m.status))) {
      if (!member.pending.includes(m.id)) return;
      member.pending = member.pending.filter(id => id !== m.id); ws.serializeAttachment(member);
      const device = member.routed?.[m.id];
      if (device) {
        delete member.routed![m.id]; ws.serializeAttachment(member);
        this.ctx.waitUntil(this.env.CLICKERS.get(this.env.CLICKERS.idFromName(device)).result(m.id, String(m.status)));
        return;
      }
      for (const tx of this.ctx.getWebSockets("tx")) {
        const publisher = tx.deserializeAttachment() as Member;
        if (m.id.startsWith(publisher.session + ":")) this.send(tx, { v: 1, type: "result", id: m.id, status: m.status });
      }
      return;
    }
    if (member.role !== "tx" || m.type !== "click" ||
        !Number.isSafeInteger(m.seq) || Number(m.seq) <= member.seq || Number(m.seq) > 0xffffffff ||
        (m.button !== "previous" && m.button !== "next") || !Number.isSafeInteger(m.sentAt) ||
        (m.transmitterId !== undefined && (typeof m.transmitterId !== "string" || !/^[a-f0-9]{12}$/.test(m.transmitterId)))) {
      ws.close(1008, "Invalid click"); return;
    }
    member.seq = Number(m.seq);
    ws.serializeAttachment(member);
    const id = `${member.session}:${m.seq}`, sentAt = Number(m.sentAt);
    if (sentAt > now + 100 || now - sentAt >= TTL) {
      this.send(ws, { v: 1, type: "result", id, status: "rejected" }); return;
    }
    member.warmUntil = now + 10 * 60 * 1000;
    ws.serializeAttachment(member); this.updateWarmth();
    let sent = false;
    for (const receiver of this.receivers()) {
      const target = receiver.deserializeAttachment() as Member;
      target.pending = [...target.pending.slice(-31), id]; receiver.serializeAttachment(target);
      sent = this.send(receiver, { v: 1, type: "click", id, transmitterId: m.transmitterId, button: m.button, sentAt, expiresAt: sentAt + TTL }) || sent;
    }
    // Never queue clicks for an offline or paused receiver.
    if (!sent) this.send(ws, { v: 1, type: "result", id, status: "rejected" });
  }

  webSocketClose(ws: WebSocket, code: number): void {
    const member = ws.deserializeAttachment() as Member;
    if (member) { member.armed = false; ws.serializeAttachment(member); }
    try { ws.close(code === 1005 ? 1000 : code); } catch {}
    this.presence();
  }
  webSocketError(ws: WebSocket): void { this.webSocketClose(ws, 1011); }
}
