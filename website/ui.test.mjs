import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { initPricing, localCurrency } from './dist/pricing.mjs';
import { initDiagram } from './dist/diagram.mjs';
import worker from './worker.js';

function element() {
  return {
    value: '', textContent: '', hidden: false, attributes: {}, events: {}, children: [],
    setAttribute(key, value) { this.attributes[key] = value; },
    getAttribute(key) { return this.attributes[key]; },
    addEventListener(type, handler) { this.events[type] = handler; },
    append(child) { this.children.push(child); },
    focus() { this.focused = true; }
  };
}
function fixture() {
  const nodes = new Map();
  const get = id => { if (!nodes.has(id)) nodes.set(id, element()); return nodes.get(id); };
  return { get, document: { querySelector: get, createElement: element } };
}
const json = data => ({ ok: true, json: async () => data });
const rate = (quote, value = 0.7) => json({ base: 'AUD', quote, rate: value, date: '2026-10-30' });

test('country takes precedence over browser region, with a safe AUD fallback', () => {
  assert.equal(localCurrency('NZ', ['en-US']), 'NZD');
  assert.equal(localCurrency(null, ['en-GB']), 'GBP');
  assert.equal(localCurrency('BG', ['en-US']), 'EUR');
  assert.equal(localCurrency('XX', ['invalid_locale', 'en', 'ja-JP']), 'JPY');
  assert.equal(localCurrency(null, ['en', 'und']), 'AUD');
});

test('local prices convert the annual AUD amount and allow manual selection', async () => {
  const { get, document } = fixture();
  await initPricing(document, { languages: ['en-US'] }, async url => url === '/api/country' ? json({ country: 'US' }) : rate('USD'));
  assert.equal(get('#annual-price').textContent, '$17.50');
  assert.equal(get('#price-code').textContent, 'USD');
  assert.match(get('#exchange-note').textContent, /Estimate.*Billed at AUD \$25\/year/);
  get('#price-currency').value = 'AUD';
  await get('#price-currency').events.change();
  assert.equal(get('#annual-price').textContent, '$25');
  assert.equal(get('#price-code').textContent, 'AUD');
  assert.equal(get('#price-display').attributes['aria-busy'], 'false');
});

test('unavailable or invalid rates leave the correctly labelled AUD price', async () => {
  for (const response of [() => { throw new Error('offline'); }, () => ({ ok: false }), () => rate('USD', -1), () => rate('GBP')]) {
    const { get, document } = fixture();
    await initPricing(document, { languages: ['en-US'] }, async url => url === '/api/country' ? { ok: false } : response());
    assert.equal(get('#annual-price').textContent, '$25');
    assert.equal(get('#price-code').textContent, 'AUD');
    assert.match(get('#exchange-note').textContent, /unavailable.*Showing AUD/);
  }
});

test('an old exchange request cannot overwrite a newer currency selection', async () => {
  const { get, document } = fixture();
  let resolveOld;
  await initPricing(document, { languages: ['en-AU'] }, async url => {
    if (url === '/api/country') return json({ country: 'AU' });
    if (url.endsWith('USD')) return new Promise(resolve => { resolveOld = resolve; });
    return rate('JPY', 100);
  });
  get('#price-currency').value = 'USD';
  const old = get('#price-currency').events.change();
  get('#price-currency').value = 'JPY';
  await get('#price-currency').events.change();
  resolveOld(rate('USD'));
  await old;
  assert.equal(get('#price-code').textContent, 'JPY');
  assert.equal(get('#annual-price').textContent, '¥2,500');
});

test('country lookup cannot override a manual selection made while it loads', async () => {
  const { get, document } = fixture();
  let resolveCountry;
  const loading = initPricing(document, { languages: ['en-AU'] }, async url => url === '/api/country'
    ? new Promise(resolve => { resolveCountry = resolve; }) : rate('USD'));
  get('#price-currency').value = 'USD';
  await get('#price-currency').events.change();
  resolveCountry(json({ country: 'JP' }));
  await loading;
  assert.equal(get('#price-code').textContent, 'USD');
  assert.equal(get('#price-currency').value, 'USD');
});

test('diagram tabs switch panels and support arrow, Home and End keys', () => {
  const tabs = [element(), element()];
  const panels = [element(), element()];
  tabs.forEach((tab, i) => tab.setAttribute('aria-controls', String(i)));
  initDiagram({ querySelectorAll: () => tabs, getElementById: id => panels[id], querySelector: element });
  tabs[1].events.click();
  assert.equal(panels[0].hidden, true);
  assert.equal(panels[1].hidden, false);
  for (const [index, key, expected] of [[1, 'ArrowRight', 0], [0, 'ArrowLeft', 1], [1, 'Home', 0], [0, 'End', 1]]) {
    tabs[index].events.keydown({ key, preventDefault() {} });
    assert.equal(tabs[expected].attributes['aria-selected'], 'true');
    assert.equal(tabs[expected].tabIndex, 0);
    assert.equal(tabs[expected].focused, true);
    assert.equal(panels[expected].hidden, false);
    assert.equal(panels[1 - expected].hidden, true);
  }
});

test('country endpoint is private and preserves existing asset and app routes', async () => {
  const request = new Request('https://uberclicker.com/api/country');
  request.cf = { country: 'NZ' };
  const response = await worker.fetch(request, {});
  assert.deepEqual(await response.json(), { country: 'NZ' });
  assert.equal(response.headers.get('Cache-Control'), 'no-store');
  const redirect = await worker.fetch(new Request('https://uberclicker.com/app/'), {});
  assert.equal(redirect.headers.get('location'), 'https://app.uberclicker.com/#configure');
  const asset = await worker.fetch(new Request('https://uberclicker.com/'), { ASSETS: { fetch: () => new Response('asset') } });
  assert.equal(await asset.text(), 'asset');
});

test('testing notices are removed, with three connection options and the requested headline present', async () => {
  const html = await readFile(new URL('./dist/index.html', import.meta.url), 'utf8');
  assert.match(html, /Slide Control<br>Done Right/);
  assert.match(html, /Robust\. Flexible\.<br>Open Source\./);
  assert.match(html, /Cloud to the Companion App/);
  assert.match(html, /LoRa receiver to keyboard control/);
  assert.match(html, /OSC over Wi-Fi to slide software/);
  assert.match(html, /class="pro-badge"/);
  assert.match(html, /id="advanced-diagram"[^>]+hidden/);
  const installer = await readFile(new URL('./dist/flash/index.html', import.meta.url), 'utf8');
  assert.doesNotMatch(html + installer, /experimental|pending|hardware.validation|acceptance|in.testing/i);
  assert.match(installer, /id="erase-consent"/);
});

test('legacy installer entry points redirect to the web app Install page', async () => {
  for (const path of ['/flash', '/flash/']) {
    const response = await worker.fetch(new Request(`https://uberclicker.com${path}`), {});
    assert.equal(response.status, 302);
    assert.equal(response.headers.get('location'), 'https://app.uberclicker.com/#install');
  }
});
