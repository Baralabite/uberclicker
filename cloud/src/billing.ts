import Stripe from "stripe";
import type { Env } from "./index";

export type Billing = { customer_id: string | null; status: string; pro_until: number; trial_used: number };
export const stripe = (env: Env) => new Stripe(env.STRIPE_SECRET_KEY, { httpClient: Stripe.createFetchHttpClient() });
export const hasPro = (billing: { status: string; pro_until: number } | null) => !!billing && ["active", "trialing"].includes(billing.status) && billing.pro_until > Date.now();
export async function getBilling(env: Env, user: string): Promise<Billing | null> {
  return env.DB.prepare("SELECT * FROM billing WHERE user_id = ?").bind(user).first<Billing>();
}
export function trialEnd(now = new Date()): number {
  const end = new Date(now), day = end.getUTCDate();
  end.setUTCDate(1); end.setUTCMonth(end.getUTCMonth() + 1);
  const last = new Date(Date.UTC(end.getUTCFullYear(), end.getUTCMonth() + 1, 0)).getUTCDate();
  end.setUTCDate(Math.min(day, last));
  return Math.floor(end.getTime() / 1000);
}
export async function syncBilling(env: Env, customer: string) {
  const row = await env.DB.prepare("SELECT user_id FROM billing WHERE customer_id = ?").bind(customer).first<{ user_id: string }>();
  if (!row) return;
  // Retrieve current Stripe state: late or repeated events must not restore an old entitlement.
  const subscriptions: Stripe.Subscription[] = [];
  for await (const sub of stripe(env).subscriptions.list({ customer, status: "all", limit: 100 })) subscriptions.push(sub);
  const rooms = await env.DB.prepare("SELECT id, revoked FROM rooms WHERE user_id = ?").bind(row.user_id).all<{ id: string; revoked: number }>();
  for (const room of rooms.results) {
    const relevant = subscriptions.filter(s => s.metadata.uberclicker_room === room.id && s.items.data.some(i => [env.STRIPE_MONTHLY_PRICE, env.STRIPE_YEARLY_PRICE].includes(i.price.id)));
    const valid = relevant.filter(s => ["active", "trialing"].includes(s.status));
    const until = Math.max(0, ...valid.flatMap(s => s.items.data.filter(i => [env.STRIPE_MONTHLY_PRICE, env.STRIPE_YEARLY_PRICE].includes(i.price.id)).map(i => (s.status === "trialing" ? s.trial_end ?? 0 : i.current_period_end) * 1000)));
    const subscription = valid.find(s => s.status === "active") ?? valid[0] ?? relevant[0];
    await env.DB.prepare("UPDATE rooms SET status = ?, pro_until = ?, trial_used = MAX(trial_used, ?), subscription_id = ? WHERE id = ?")
      .bind(subscription?.status ?? "free", until, relevant.some(s => s.trial_start !== null) ? 1 : 0, subscription?.id ?? null, room.id).run();
    await env.ROOMS.get(env.ROOMS.idFromName(room.id)).refreshEntitlement(room.id);
    if (room.revoked) for (const sub of relevant) {
      if (!["canceled", "incomplete_expired"].includes(sub.status) && !sub.cancel_at_period_end) await stripe(env).subscriptions.update(sub.id, { cancel_at_period_end: true });
    }
  }
}
export async function checkout(env: Env, user: { id: string; email: string }, interval: string, roomId: string, desktop = false) {
  if (interval !== "year") throw new Error("Only yearly billing is available: AUD $25/year per channel");
  const room = await env.DB.prepare("SELECT * FROM rooms WHERE id = ? AND user_id = ? AND revoked = 0").bind(roomId, user.id).first<{ id: string; status: string; pro_until: number; trial_used: number }>();
  if (!room) throw new Error("Channel not found");
  const returnUrl = `${env.APP_ORIGIN}/${desktop ? "desktop-billing" : "#account"}`;
  const api = stripe(env), price = env.STRIPE_YEARLY_PRICE;
  if (!env.STRIPE_SECRET_KEY || !env.STRIPE_WEBHOOK_SECRET || !price) throw new Error("Subscriptions are not available yet");
  await env.DB.prepare("INSERT OR IGNORE INTO billing (user_id) VALUES (?)").bind(user.id).run();
  let billing = (await getBilling(env, user.id))!;
  if (!billing.customer_id) {
    const customer = await api.customers.create({ email: user.email, metadata: { uberclicker_user: user.id } }, { idempotencyKey: `uberclicker-customer-${user.id}` });
    await env.DB.prepare("UPDATE billing SET customer_id = ? WHERE user_id = ?").bind(customer.id, user.id).run();
    billing = (await getBilling(env, user.id))!;
  }
  await syncBilling(env, billing.customer_id!);
  billing = (await getBilling(env, user.id))!;
  const updated = await env.DB.prepare("SELECT * FROM rooms WHERE id = ?").bind(roomId).first<typeof room>();
  if (hasPro(updated) || ["past_due", "unpaid", "incomplete", "paused"].includes(updated!.status)) {
    return (await api.billingPortal.sessions.create({ customer: billing.customer_id!, return_url: returnUrl })).url;
  }
  const open = await api.checkout.sessions.list({ customer: billing.customer_id!, status: "open", limit: 100 });
  const pending = open.data.find(s => s.metadata?.uberclicker_room === roomId);
  if (pending?.url && pending.metadata?.uberclicker_interval === interval && (pending.metadata?.uberclicker_desktop === "true") === desktop) return pending.url;
  if (pending) await api.checkout.sessions.expire(pending.id);
  const session = await api.checkout.sessions.create({
    customer: billing.customer_id!, mode: "subscription", line_items: [{ price, quantity: 1 }],
    success_url: desktop ? returnUrl : `${env.APP_ORIGIN}/?checkout=success#account`, cancel_url: returnUrl,
    client_reference_id: user.id, metadata: { uberclicker_room: roomId, uberclicker_interval: interval, uberclicker_desktop: String(desktop) }, payment_method_collection: "always",
    subscription_data: { metadata: { uberclicker_user: user.id, uberclicker_room: roomId }, ...(updated!.trial_used ? {} : { trial_end: trialEnd() }) },
    expires_at: Math.floor(Date.now() / 1000) + 1800,
  }, { idempotencyKey: `uberclicker-checkout-${roomId}-${interval}-${desktop ? "desktop" : "web"}-${pending?.id ?? "new"}-${Math.floor(Date.now() / 1800000)}` });
  return session.url;
}
export async function revokeDestination(env: Env, user: string, room: string) {
  const owned = await env.DB.prepare("SELECT id FROM rooms WHERE id = ? AND user_id = ?").bind(room, user).first();
  if (!owned) throw new Error("Channel not found");
  await env.DB.prepare("UPDATE rooms SET revoked = 1 WHERE id = ?").bind(room).run();
  await env.ROOMS.get(env.ROOMS.idFromName(room)).refreshEntitlement(room);
  const billing = await getBilling(env, user);
  if (!billing?.customer_id) return;
  const api = stripe(env);
  for await (const session of api.checkout.sessions.list({ customer: billing.customer_id, status: "open", limit: 100 })) {
    if (session.metadata?.uberclicker_room !== room) continue;
    try { await api.checkout.sessions.expire(session.id); }
    catch (error) { if ((await api.checkout.sessions.retrieve(session.id)).status === "open") throw error; }
  }
  // Includes a Checkout completed just before expiry; later webhooks also cancel revoked destinations.
  await syncBilling(env, billing.customer_id);
}
export async function webhook(request: Request, env: Env) {
  if (!env.STRIPE_WEBHOOK_SECRET) return new Response("Billing unavailable", { status: 503 });
  let event: Stripe.Event;
  try { event = await stripe(env).webhooks.constructEventAsync(await request.text(), request.headers.get("stripe-signature") ?? "", env.STRIPE_WEBHOOK_SECRET, undefined, Stripe.createSubtleCryptoProvider()); }
  catch { return new Response("Invalid signature", { status: 400 }); }
  if (event.type.startsWith("customer.subscription.") || event.type === "checkout.session.completed" || event.type.startsWith("invoice.")) {
    const customer = (event.data.object as { customer?: string }).customer;
    if (typeof customer === "string") {
      const row = await env.DB.prepare("SELECT user_id FROM billing WHERE customer_id = ?").bind(customer).first<{ user_id: string }>();
      if (row) await env.BILLING.get(env.BILLING.idFromName(row.user_id)).sync(customer);
    }
  }
  return Response.json({ received: true });
}
