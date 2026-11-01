import { test } from 'node:test';
import assert from 'node:assert/strict';
import { checkout, hasPro, trialEnd } from '../src/billing.ts';

test('checkout rejects monthly billing before contacting Stripe or the database', async () => {
  for (const interval of ['month', '', 'week']) {
    await assert.rejects(checkout({}, { id: 'owner', email: 'owner@example.com' }, interval, 'channel'), /Only yearly billing/);
  }
});

test('checkout uses one annual price, replaces a monthly checkout, and preserves the first free month', async () => {
  const room = { id: 'channel', status: 'free', pro_until: 0, trial_used: 0 };
  const originalFetch = globalThis.fetch, requests = [];
  const env = {
    STRIPE_SECRET_KEY: 'sk_test_local', STRIPE_WEBHOOK_SECRET: 'whsec_local', STRIPE_YEARLY_PRICE: 'price_year',
    APP_ORIGIN: 'https://app.uberclicker.com',
    DB: { prepare(query) { return {
      bind() { return this; },
      async first() { return query.includes('FROM billing') ? { user_id: 'owner', customer_id: 'cus_owner' } : room; },
      async all() { return { results: [] }; },
      async run() {},
    }; } },
  };
  globalThis.fetch = async (url, init) => {
    const path = new URL(String(url)).pathname, method = init?.method ?? 'GET';
    const body = new URLSearchParams(init?.body);
    requests.push({ path, method, body });
    let response;
    if (path === '/v1/subscriptions') response = { object: 'list', data: [], has_more: false };
    else if (path === '/v1/checkout/sessions' && method === 'GET') response = { object: 'list', has_more: false, data: [{ id: 'cs_month', url: 'https://checkout.stripe.com/month', metadata: { uberclicker_room: room.id, uberclicker_interval: 'month' } }] };
    else if (path === '/v1/checkout/sessions/cs_month/expire') response = { id: 'cs_month', status: 'expired' };
    else if (path === '/v1/checkout/sessions' && method === 'POST') response = { id: 'cs_year', url: 'https://checkout.stripe.com/year' };
    else throw Error(`Unexpected Stripe request: ${method} ${path}`);
    return new Response(JSON.stringify(response), { headers: { 'content-type': 'application/json' } });
  };
  try {
    for (const trialUsed of [0, 1]) {
      room.trial_used = trialUsed;
      assert.equal(await checkout(env, { id: 'owner', email: 'owner@example.com' }, 'year', room.id), 'https://checkout.stripe.com/year');
      const body = requests.at(-1).body;
      assert.equal(body.get('line_items[0][price]'), 'price_year');
      assert.equal(body.get('line_items[0][quantity]'), '1');
      assert.equal(body.get('metadata[uberclicker_interval]'), 'year');
      assert.equal(body.get('subscription_data[metadata][uberclicker_room]'), room.id);
      assert.equal(body.get('payment_method_collection'), 'always');
      if (trialUsed) assert.equal(body.has('subscription_data[trial_end]'), false);
      else assert.ok(Math.abs(Number(body.get('subscription_data[trial_end]')) - trialEnd()) <= 1);
      assert.equal(requests.at(-2).path, '/v1/checkout/sessions/cs_month/expire');
    }
  } finally { globalThis.fetch = originalFetch; }
});

test('only a current active or trial subscription unlocks a destination', () => {
  for (const status of ['free', 'canceled', 'unpaid', 'past_due', 'incomplete', 'paused']) assert.equal(hasPro({ status, pro_until: Date.now() + 60000 }), false);
  for (const status of ['active', 'trialing']) {
    assert.equal(hasPro({ status, pro_until: Date.now() + 60000 }), true);
    assert.equal(hasPro({ status, pro_until: Date.now() - 1 }), false);
  }
  assert.equal(hasPro(null), false);
});
test('one calendar month trial clamps month end and preserves UTC time', () => {
  for (const [from, to] of [['2026-01-31T12:30:00Z','2026-02-28T12:30:00Z'], ['2028-01-31T12:30:00Z','2028-02-29T12:30:00Z'], ['2026-11-01T12:30:00Z','2026-12-01T12:30:00Z'], ['2026-12-31T12:30:00Z','2027-01-31T12:30:00Z']]) assert.equal(trialEnd(new Date(from)), Date.parse(to)/1000);
});

test('reconciliation reads current subscriptions; revocation expires checkouts and stops renewals', async () => {
  const { syncBilling, revokeDestination } = await import('../src/billing.ts');
  const destination = { id: 'destination', revoked: 0, status: 'free', pro_until: 0, trial_used: 0 };
  const expires = Math.floor(Date.now() / 1000) + 3600;
  let subscription = { id: 'sub_current', status: 'trialing', trial_start: expires - 3600, trial_end: expires, cancel_at_period_end: false, metadata: { uberclicker_room: destination.id }, items: { data: [{ price: { id: 'price_month' }, current_period_end: expires }] } };
  const calls = [], entitlements = [], originalFetch = globalThis.fetch;
  globalThis.fetch = async (url, init) => {
    const path = new URL(typeof url === 'string' ? url : url.url ?? String(url)).pathname;
    calls.push([init?.method ?? 'GET', path]);
    let response;
    if (path === '/v1/subscriptions') response = { object: 'list', data: [subscription], has_more: false };
    else if (path === '/v1/checkout/sessions') response = { object: 'list', data: [{ id: 'cs_pending', metadata: { uberclicker_room: destination.id } }], has_more: false };
    else if (path === '/v1/checkout/sessions/cs_pending/expire') response = { id: 'cs_pending', status: 'expired' };
    else if (path === '/v1/subscriptions/sub_current') response = { ...subscription, cancel_at_period_end: true };
    else throw Error(`Unexpected Stripe request: ${path}`);
    return new Response(JSON.stringify(response), { headers: { 'content-type': 'application/json' } });
  };
  const env = {
    STRIPE_SECRET_KEY: 'sk_test_local', STRIPE_MONTHLY_PRICE: 'price_month', STRIPE_YEARLY_PRICE: 'price_year',
    DB: { prepare(query) { let args; return {
      bind(...values) { args = values; return this; },
      async first() { return query.includes('FROM billing') ? { user_id: 'owner', customer_id: 'cus_owner' } : destination; },
      async all() { return { results: [destination] }; },
      async run() {
        if (query.includes('SET revoked')) destination.revoked = 1;
        else if (query.includes('SET status')) Object.assign(destination, { status: args[0], pro_until: args[1], trial_used: Math.max(destination.trial_used, args[2]), subscription_id: args[3] });
      },
    }; } },
    ROOMS: { idFromName: id => id, get: () => ({ refreshEntitlement: async () => entitlements.push(destination.revoked ? 0 : destination.pro_until) }) },
  };
  try {
    await syncBilling(env, 'cus_owner');
    assert.equal(destination.status, 'trialing'); assert.equal(destination.trial_used, 1);
    assert.equal(entitlements.at(-1), expires * 1000);
    subscription = { ...subscription, status: 'canceled' };
    await syncBilling(env, 'cus_owner');
    assert.equal(entitlements.at(-1), 0, 'a delayed event cannot restore canceled access');
    subscription = { ...subscription, status: 'trialing' };
    await revokeDestination(env, 'owner', destination.id);
    assert.equal(destination.revoked, 1); assert.equal(entitlements.at(-1), 0);
    assert.ok(calls.some(([method, path]) => method === 'POST' && path.endsWith('/cs_pending/expire')));
    assert.ok(calls.some(([method, path]) => method === 'POST' && path.endsWith('/sub_current')));
  } finally { globalThis.fetch = originalFetch; }
});
