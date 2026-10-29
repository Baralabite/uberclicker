import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import vm from 'node:vm';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), 'dist');
const html = readFileSync(path.join(root, 'index.html'), 'utf8');
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
assert.equal(new Set(ids).size, ids.length, 'HTML ids must be unique');
for (const [, anchor] of html.matchAll(/href="#([^"]+)"/g)) {
  assert(ids.includes(anchor), `Missing anchor: ${anchor}`);
}
for (const [, asset] of html.matchAll(/(?:src|href)="(?!#)([^":]+)"/g)) {
  assert(existsSync(path.join(root, asset)), `Missing local asset: ${asset}`);
}
const nodes = new Map();
function node(selector) {
  if (!nodes.has(selector)) nodes.set(selector, { textContent: '', events: {}, addEventListener(type, handler) { this.events[type] = handler; } });
  return nodes.get(selector);
}
const radios = ['ble', 'lan', 'linked'].map(value => ({ ...node(value), value }));
const buyLists = [...html.matchAll(/<ul id="(buy-[^"]+)"[^>]*>([\s\S]*?)<\/ul>/g)].map(([, id, body]) => {
  assert.equal([...body.matchAll(/<a href="https:\/\//g)].length, 3, `${id} needs three product links`);
  return Object.assign(node(`#${id}`), { id });
});
assert.equal(buyLists.length, 4, 'Four buying regions must be available');
assert.match(html, /<option value="buy-au" selected>Australia<\/option>/, 'Australia is the default');
node('#buy-country').value = 'buy-au';
const document = {
  querySelector: node,
  querySelectorAll: selector => selector === '.buy-options' ? buyLists : radios
};
vm.runInNewContext(readFileSync(path.join(root, 'app.js'), 'utf8'), { document, setTimeout() {} });
node('#next-cue').events.click();
assert.equal(node('#last-action').textContent, 'Last: Next');
assert.match(node('#last-receipt').textContent, /simulated/);
node('#previous-cue').events.click();
assert.equal(node('#last-action').textContent, 'Last: Previous');
radios[1].events.change();
assert.match(node('#connection-detail').textContent, /LAN configuration is unavailable/);
radios[2].events.change();
assert.match(node('#connection-detail').textContent, /alone does not make a device reachable/);
assert(!/023|82%|LoRa paired|cue-count/.test(html), 'No invented device status or cue counter');
assert.deepEqual(buyLists.filter(list => !list.hidden).map(list => list.id), ['buy-au']);
for (const region of ['buy-nz', 'buy-us', 'buy-eu', 'buy-au']) {
  node('#buy-country').value = region;
  node('#buy-country').events.change();
  assert.deepEqual(buyLists.filter(list => !list.hidden).map(list => list.id), [region], 'Only the selected region is shown');
}
console.log('Pass: local assets, anchors, unique ids, sample actions, connection choices, truthful demo status, buying regions and three links per region.');
