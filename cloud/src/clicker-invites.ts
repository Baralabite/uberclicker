import type { Env } from "./index";
import { hash, hex, json } from "./accounts";

async function key(env: Env) {
  return crypto.subtle.importKey("raw", new TextEncoder().encode(env.BETTER_AUTH_SECRET), { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
}
const payload = (id: string, expires: number) => new TextEncoder().encode(`clicker-invite-v1:${id}.${expires}`);

export async function createClickerInvite(env: Env, user: string, room: string): Promise<Response> {
  if (!await env.DB.prepare("SELECT id FROM rooms WHERE id = ? AND user_id = ? AND revoked = 0").bind(room, user).first()) return json({ error: "Channel not found" }, 404);
  const id = hex(16), expires = Date.now() + 600000;
  const signature = Array.from(new Uint8Array(await crypto.subtle.sign("HMAC", await key(env), payload(id, expires))), b => b.toString(16).padStart(2, "0")).join("");
  await env.DB.batch([
    env.DB.prepare("DELETE FROM clicker_invites WHERE expires < ?").bind(Date.now() - 86400000),
    env.DB.prepare("INSERT INTO clicker_invites (id, room_id, user_id, expires) VALUES (?, ?, ?, ?)").bind(id, room, user, expires),
  ]);
  return json({ url: `${env.APP_ORIGIN}/#send?invite=${id}.${expires}.${signature}`, expires });
}

export async function redeemClickerInvite(env: Env, invite: unknown, id: string, token: string, name: string): Promise<Response> {
  if (typeof invite !== "string" || !/^[a-f0-9]{32}\.\d{13}\.[a-f0-9]{64}$/.test(invite)) return json({ error: "Invalid clicker link. Ask for a new link." }, 400);
  const [nonce, expiry, signature] = invite.split("."), expires = Number(expiry);
  const bytes = Uint8Array.from(signature.match(/../g)!, b => parseInt(b, 16));
  if (!await crypto.subtle.verify("HMAC", await key(env), bytes, payload(nonce, expires))) return json({ error: "Invalid clicker link. Ask for a new link." }, 400);
  const link = await env.DB.prepare("SELECT clicker_invites.* FROM clicker_invites JOIN rooms ON rooms.id = clicker_invites.room_id WHERE clicker_invites.id = ? AND clicker_invites.expires = ? AND rooms.user_id = clicker_invites.user_id AND rooms.revoked = 0")
    .bind(nonce, expires).first<{ room_id: string; user_id: string; redeemed_clicker: string | null }>();
  if (!link) return json({ error: "This clicker link is no longer available. Ask for a new link." }, 400);
  const digest = await hash(token);
  // Repeating the same authenticated request recovers a lost response without linking a second device.
  if (link.redeemed_clicker === id && await env.DB.prepare("SELECT id FROM clickers WHERE id = ? AND token_hash = ? AND user_id = ? AND revoked = 0").bind(id, digest, link.user_id).first()) return json({ v: 1, room: id, token });
  if (expires <= Date.now() || link.redeemed_clicker) return json({ error: "This clicker link expired or was already used. Ask for a new link." }, 400);
  if (await env.DB.prepare("SELECT id FROM clickers WHERE id = ?").bind(id).first()) return json({ error: "Clicker already registered" }, 409);
  const now = Date.now();
  const result = await env.DB.batch([
    env.DB.prepare("INSERT INTO clickers (id, token_hash, name, user_id, created_at, last_seen) SELECT ?, ?, ?, clicker_invites.user_id, ?, ? FROM clicker_invites JOIN rooms ON rooms.id = clicker_invites.room_id WHERE clicker_invites.id = ? AND redeemed_clicker IS NULL AND expires > ? AND rooms.revoked = 0 AND rooms.user_id = clicker_invites.user_id").bind(id, digest, name, now, now, nonce, now),
    env.DB.prepare("UPDATE clicker_invites SET redeemed_clicker = ? WHERE id = ? AND redeemed_clicker IS NULL AND EXISTS (SELECT 1 FROM clickers WHERE id = ? AND token_hash = ?)").bind(id, nonce, id, digest),
    env.DB.prepare("INSERT INTO clicker_routes (clicker_id, room_id) SELECT ?, room_id FROM clicker_invites WHERE id = ? AND redeemed_clicker = ?").bind(id, nonce, id),
  ]);
  if (!result[0].meta.changes) return json({ error: "This clicker link was already used. Ask for a new link." }, 400);
  return json({ v: 1, room: id, token }, 201);
}
