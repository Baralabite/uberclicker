import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { sha256, validateManifest, validateImage, verifyRelease, checkDevice } from './dist/flash/validate.mjs';
import { FlashSession } from './dist/flash/session.mjs';

const bytes = new Uint8Array(128); bytes[0] = 0xe9; bytes[12] = 9;
new DataView(bytes.buffer).setUint32(32, 0xabcd5432, true);
bytes.set(new TextEncoder().encode('0.1.0-test'), 48); bytes.set(new TextEncoder().encode('UberClicker'), 80);
const image = (file, offset) => ({ file, offset, size: bytes.length, sha256: hash });
const hash = await sha256(bytes);
const manifest = { manifestVersion: 1, version: '0.1.0-test', channel: 'prerelease', board: 'thinknode_m5', chip: 'esp32s3', flashSize: 4194304, sourceCommit: 'a'.repeat(40), sourceDirty: false, hardwareValidated: false, settingsSchemaVersion: 1, configProtocolVersion: 1, radioProtocolVersion: 1,
  images: [image('bootloader.bin', 0), image('partitions.bin', 0x8000), image('firmware.bin', 0x10000)],
  partitions: [{ name: 'nvs', type: 1, subtype: 2, offset: 0x9000, size: 0x5000 }, { name: 'app', type: 0, subtype: 0, offset: 0x10000, size: 0x300000 }] };
const release = { manifest, files: manifest.images.map(i => ({ data: bytes, address: i.offset })) };
function fixture(overrides = {}) {
  let writes = 0;
  const loader = { chip: { CHIP_NAME: 'ESP32-S3' }, async main() {}, async detectFlashSize() { return '4MB'; }, async readFlash() { return bytes; }, async writeFlash() { writes++; }, async after() {}, ...overrides };
  return { session: new FlashSession(release, loader, { async disconnect() {} }), writes: () => writes };
}
test('valid release and exact hashes', async () => { validateManifest(manifest); await validateImage(manifest.images[0], bytes); });
test('v2 schema, logical protocol, BLE framing, radio management and migration stay distinct', () => {
  const v2 = { ...structuredClone(manifest), settingsSchemaVersion: 2, configProtocolVersion: 2,
    bleFramingVersion: 1, remoteConfigRadioVersion: 2, preserveSettingsSchemas: [1, 2], settingsMigration: 'atomic-on-apply-v1-to-v2' };
  validateManifest(v2);
  assert.throws(() => validateManifest({ ...v2, configProtocolVersion: 1 }));
  assert.throws(() => validateManifest({ ...v2, remoteConfigRadioVersion: 1 }));
  assert.throws(() => validateManifest({ ...v2, preserveSettingsSchemas: [2] }));
  assert.throws(() => validateManifest({ ...manifest, remoteConfigRadioVersion: 2 }));
});
test('manifest wrong chip, bounds, overlap, traversal and version rejected', () => {
  for (const mutate of [m => m.chip = 'esp32', m => m.images[2].offset = 4194304, m => m.images[2].offset = 0x8000, m => m.images[0].file = '../bootloader.bin', m => m.version = 'stable']) {
    const copy = structuredClone(manifest); mutate(copy); assert.throws(() => validateManifest(copy));
  }
});
test('corrupted hash and wrong-chip image rejected', async () => {
  const corrupt = bytes.slice(); corrupt[1] = 3;
  await assert.rejects(validateImage(manifest.images[0], corrupt));
  corrupt[12] = 0;
  await assert.rejects(validateImage({ ...manifest.images[0], sha256: await sha256(corrupt) }, corrupt), /ESP32-S3/);
});
test('application descriptor must identify the selected release and UberClicker', async () => {
  const application = manifest.images.find(i => i.file === 'firmware.bin');
  await validateImage(application, bytes, manifest.version);
  await assert.rejects(validateImage(application, bytes, '0.1.0-other'), /descriptor/);
  const other = bytes.slice(); other[80] = 88;
  await assert.rejects(validateImage({ ...application, sha256: await sha256(other) }, other, manifest.version), /descriptor/);
});
test('manifest corruption rejected before image fetching', async () => {
  let fetches = 0;
  await assert.rejects(verifyRelease({ version: manifest.version, channel: 'prerelease', manifestSha256: '0'.repeat(64) }, async () => { fetches++; return bytes; }), /Manifest SHA/);
  assert.equal(fetches, 1);
});
test('wrong chip and flash size refuse writes', async () => {
  assert.throws(() => checkDevice('ESP32', '4MB'));
  const f = fixture({ async detectFlashSize() { return '8MB'; } });
  await assert.rejects(f.session.connect('preserve', false)); await assert.rejects(f.session.write()); assert.equal(f.writes(), 0);
});
test('preserve mismatch never falls back to erase', async () => {
  const f = fixture({ async readFlash() { return new Uint8Array(64); } });
  await assert.rejects(f.session.connect('preserve', false), /layout differs/); await assert.rejects(f.session.write()); assert.equal(f.writes(), 0);
});
test('explicit erase consent, disconnect and cancellation', async () => {
  const f = fixture(); await assert.rejects(f.session.connect('erase', false)); assert.equal(f.writes(), 0);
  await f.session.connect('preserve', false); await f.session.disconnect(); await assert.rejects(f.session.write());
  const cancelled = fixture({ async main() { throw new DOMException('cancelled', 'NotFoundError'); } });
  await assert.rejects(cancelled.session.connect('preserve', false)); assert.equal(cancelled.writes(), 0);
});
test('verified write, failed readback, failed transport', async () => {
  const f = fixture(); await f.session.connect('preserve', false); await f.session.write(); assert.equal(f.writes(), 1);
  const bad = fixture({ async readFlash() { return new Uint8Array(64); } }); await bad.session.connect('erase', true); await assert.rejects(bad.session.write(), /Read-back/);
  const lost = fixture({ async writeFlash() { throw new Error('disconnected'); } }); await lost.session.connect('preserve', false); await assert.rejects(lost.session.write(), /disconnected/); assert.equal(lost.session.ready, false);
});
test('unsupported browser executes safely with an empty published index', async () => {
  const originals = new Map(['document', 'navigator', 'isSecureContext', 'Option', 'fetch'].map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  const nodes = new Map();
  const get = id => { if (!nodes.has(id)) nodes.set(id, { value: id === 'channel' ? 'stable' : '', events: {}, addEventListener(type, handler) { this.events[type] = handler; }, replaceChildren() {}, add() {} }); return nodes.get(id); };
  try {
    Object.defineProperty(globalThis, 'navigator', { configurable: true, value: {} });
    Object.assign(globalThis, { document: { getElementById: get }, isSecureContext: false, Option: class {}, fetch: async () => ({ ok: true, headers: new Map(), async arrayBuffer() { return new TextEncoder().encode('{"indexVersion":1,"releases":[]}').buffer; } }) });
    await import('./dist/flash/install.mjs?unsupported-test');
    assert.match(get('support').textContent, /This browser cannot flash/);
    assert.match(get('release-detail').textContent, /No published releases/);
    assert.equal(get('connect').disabled, true); assert.equal(get('write').disabled, true);
  } finally {
    for (const [key, descriptor] of originals) { if (descriptor) Object.defineProperty(globalThis, key, descriptor); else delete globalThis[key]; }
  }
});
