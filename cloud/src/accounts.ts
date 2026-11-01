import type { Env } from "./index";
import { auth } from "./auth";
import { clickerAccount } from "./clickers";
import { getBilling, hasPro, stripe, webhook } from "./billing";

export const hex = (n: number) => Array.from(crypto.getRandomValues(new Uint8Array(n)), b => b.toString(16).padStart(2, "0")).join("");
export async function hash(value: string): Promise<string> {
  return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value))), b => b.toString(16).padStart(2, "0")).join("");
}
export const json = (body: unknown, status = 200) => Response.json(body, { status, headers: { "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff" } });
export async function accounts(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url), path = url.pathname;
  if (path === "/api/billing/webhook" && request.method === "POST") return webhook(request, env);
  if (path === "/api/config") return json({ google: !!(env.GOOGLE_CLIENT_ID && env.GOOGLE_CLIENT_SECRET), email: env.EMAIL_ENABLED === "true", billing: !!(env.STRIPE_SECRET_KEY && env.STRIPE_WEBHOOK_SECRET && env.STRIPE_YEARLY_PRICE) });
  if (url.origin !== env.APP_ORIGIN) return json({ error: "Use app.uberclicker.com for your account" }, 403);
  if (!["GET", "HEAD"].includes(request.method) && request.headers.get("Origin") !== env.APP_ORIGIN) return json({ error: "Invalid origin" }, 403);
  if (Number(request.headers.get("Content-Length") ?? 0) > 16384) return json({ error: "Request too large" }, 413);
  if (request.method !== "GET" && !(await env.AUTH_LIMIT.limit({ key: request.headers.get("CF-Connecting-IP") ?? "local" })).success) return json({ error: "Too many requests. Try again in a minute." }, 429);
  const identity = auth(env);
  if (path.startsWith("/api/auth/")) return identity.handler(request);
  const session = await identity.api.getSession({ headers: request.headers });
  if (!session) return json({ error: "Sign in to your UberClicker account" }, 401);
  const user = session.user;
  if (!user.emailVerified) return json({ error: "Verify your email first" }, 403);
  const clickerResponse = await clickerAccount(request, env, user.id);
  if (clickerResponse) return clickerResponse;
  if (path === "/api/account" && request.method === "GET") {
    const rooms = await env.DB.prepare("SELECT id, name, created_at, revoked, status, pro_until, trial_used FROM rooms WHERE user_id = ? ORDER BY created_at DESC").bind(user.id).all<{ status: string; pro_until: number; revoked: number }>();
    const devices = await env.DB.prepare("SELECT id, name, last_seen FROM clickers WHERE user_id = ? AND revoked = 0 ORDER BY last_seen DESC, created_at DESC").bind(user.id).all<{ id: string; name: string; last_seen: number }>();
    await Promise.all(devices.results.map(async d => { d.last_seen = Math.max(d.last_seen, await env.CLICKERS.get(env.CLICKERS.idFromName(d.id)).lastSeen()); }));
    devices.results.sort((a, b) => b.last_seen - a.last_seen);
    const routes = await env.DB.prepare("SELECT clicker_id, room_id FROM clicker_routes JOIN rooms ON rooms.id = clicker_routes.room_id JOIN clickers ON clickers.id = clicker_routes.clicker_id WHERE clickers.user_id = ? AND clickers.revoked = 0 AND rooms.revoked = 0").bind(user.id).all<{ clicker_id: string; room_id: string }>();
    const billing = await getBilling(env, user.id);
    return json({ devices: devices.results.map(d => ({ ...d, channels: routes.results.filter(r => r.clicker_id === d.id).map(r => r.room_id) })), user: { name: user.name, email: user.email }, pro: rooms.results.some(r => !r.revoked && hasPro(r)), billing: { hasCustomer: !!billing?.customer_id, status: billing?.status ?? "free", until: billing?.pro_until ?? 0, trialUsed: !!billing?.trial_used }, rooms: rooms.results });
  }
  if (path === "/api/billing/sync" && request.method === "POST") {
    const billing = await getBilling(env, user.id);
    if (billing?.customer_id) await env.BILLING.get(env.BILLING.idFromName(user.id)).sync(billing.customer_id);
    return json({ ok: true });
  }
  if (path === "/api/billing/checkout" && request.method === "POST") {
    const { interval, room } = await request.json<{ interval: string; room: string }>();
    return json({ url: await env.BILLING.get(env.BILLING.idFromName(user.id)).checkout(user, interval, room) });
  }
  if (path === "/api/billing/portal" && request.method === "POST") {
    const billing = await getBilling(env, user.id);
    if (!billing?.customer_id) return json({ error: "No billing account yet" }, 400);
    return json({ url: (await stripe(env).billingPortal.sessions.create({ customer: billing.customer_id, return_url: `${env.APP_ORIGIN}/#account` })).url });
  }
  if (path === "/v1/rooms" && request.method === "POST") {
    const count = await env.DB.prepare("SELECT COUNT(*) AS total FROM rooms WHERE user_id = ?").bind(user.id).first<{ total: number }>();
    if ((count?.total ?? 0) >= 20) return json({ error: "Channel limit reached. Contact support to add more." }, 409);
    if (!(await env.CREATE_LIMIT.limit({ key: user.id })).success) return json({ error: "Too many channels. Try again in a minute." }, 429);
    const { name } = await request.json<{ name: string }>();
    if (typeof name !== "string" || !name.trim() || name.length > 80) return json({ error: "Channel name must be 1–80 characters" }, 400);
    const room = hex(16), txToken = hex(32), clientToken = hex(32);
    await env.ROOMS.get(env.ROOMS.idFromName(room), { locationHint: "oc" }).initialize({ tx: await hash(txToken), client: await hash(clientToken) }, 0);
    await env.DB.prepare("INSERT INTO rooms (id, user_id, name, created_at) VALUES (?, ?, ?, ?)").bind(room, user.id, name.trim(), Date.now()).run();
    return json({ v: 1, room, txToken, clientToken }, 201);
  }
  const rename = /^\/api\/rooms\/([a-f0-9]{32})\/rename$/.exec(path);
  if (rename && request.method === "POST") {
    const { name } = await request.json<{ name: unknown }>();
    if (typeof name !== "string" || !name.trim() || name.length > 80) return json({ error: "Channel name must be 1–80 characters" }, 400);
    const changed = await env.DB.prepare("UPDATE rooms SET name = ? WHERE id = ? AND user_id = ? AND revoked = 0 RETURNING id").bind(name.trim(), rename[1], user.id).first();
    return changed ? json({ ok: true }) : json({ error: "Channel not found" }, 404);
  }
  const room = /^\/api\/rooms\/([a-f0-9]{32})\/revoke$/.exec(path);
  if (room && request.method === "POST") {
    await env.BILLING.get(env.BILLING.idFromName(user.id)).revoke(user.id, room[1]);
    return json({ ok: true });
  }
  return json({ error: "Not found" }, 404);
}
