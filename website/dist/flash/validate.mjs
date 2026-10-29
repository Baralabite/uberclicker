export const BOARD = 'thinknode_m5';
export const VERSION = /^\d+\.\d+\.\d+-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*$/;
const fail = message => { throw new Error(message); };
export async function sha256(bytes) {
  return [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map(n => n.toString(16).padStart(2, '0')).join('');
}
export function validateManifest(m) {
  if (!m || m.manifestVersion !== 1 || !VERSION.test(m.version) || m.version.length > 31 || m.channel !== 'prerelease' ||
      m.board !== BOARD || m.chip !== 'esp32s3' || m.flashSize !== 4194304 ||
      !/^[0-9a-f]{40}$/.test(m.sourceCommit) || m.sourceDirty !== false || m.hardwareValidated !== false ||
      ![1, 2].includes(m.settingsSchemaVersion) || m.configProtocolVersion !== m.settingsSchemaVersion || m.radioProtocolVersion !== 1) fail('Unsupported release metadata.');
  if (m.settingsSchemaVersion === 2 && (m.bleFramingVersion !== 1 || m.remoteConfigRadioVersion !== 2 ||
      JSON.stringify(m.preserveSettingsSchemas) !== '[1,2]' || m.settingsMigration !== 'atomic-on-apply-v1-to-v2')) fail('Unsupported v2 migration or remote-management protocol.');
  if (m.settingsSchemaVersion === 1 && ((m.bleFramingVersion !== undefined && m.bleFramingVersion !== 1) ||
      (m.remoteConfigRadioVersion !== undefined && m.remoteConfigRadioVersion !== 0) ||
      (m.preserveSettingsSchemas !== undefined && JSON.stringify(m.preserveSettingsSchemas) !== '[1]') ||
      (m.settingsMigration !== undefined && m.settingsMigration !== 'none'))) fail('Unsupported legacy protocol metadata.');
  if (!Array.isArray(m.images) || m.images.length < 3 || m.images.length > 8 || !Array.isArray(m.partitions) || m.partitions.length < 2 || m.partitions.length > 95) fail('Invalid flash layout.');
  let end = 0;
  const names = new Set();
  for (const image of [...m.images].sort((a, b) => a.offset - b.offset)) {
    if (!/^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.bin$/.test(image.file) || names.has(image.file) ||
        !Number.isSafeInteger(image.offset) || image.offset < end || image.offset % 4096 ||
        !Number.isSafeInteger(image.size) || image.size <= 0 || !/^[0-9a-f]{64}$/.test(image.sha256)) fail('Invalid or overlapping image.');
    end = image.offset + Math.ceil(image.size / 4096) * 4096;
    if (end > m.flashSize) fail('Image exceeds flash bounds.');
    names.add(image.file);
  }
  for (const name of ['bootloader.bin', 'partitions.bin', 'firmware.bin']) if (!names.has(name)) fail(`Missing ${name}.`);
  let partitionEnd = 0;
  for (const p of [...m.partitions].sort((a, b) => a.offset - b.offset)) {
    if (!Number.isSafeInteger(p.offset) || !Number.isSafeInteger(p.size) || p.size <= 0 || p.offset < partitionEnd || p.offset % 4096 ||
        p.offset + p.size > m.flashSize || !Number.isInteger(p.type) || !Number.isInteger(p.subtype)) fail('Invalid partition.');
    partitionEnd = p.offset + p.size;
  }
  const app = m.images.find(i => i.file === 'firmware.bin');
  if (!m.partitions.some(p => p.type === 0 && p.offset === app.offset && p.size >= app.size)) fail('Application does not fit partition.');
  const settings = m.partitions.filter(p => p.type === 1 && p.subtype === 2);
  if (!settings.length) fail('Missing settings partition.');
  for (const p of settings) for (const i of m.images) {
    if (i.offset < p.offset + p.size && i.offset + Math.ceil(i.size / 4096) * 4096 > p.offset) fail('Image would erase settings.');
  }
  return m;
}
export async function validateImage(image, bytes, version) {
  if (bytes.length !== image.size || await sha256(bytes) !== image.sha256) fail(`Size or SHA-256 mismatch: ${image.file}`);
  if (['bootloader.bin', 'firmware.bin'].includes(image.file) &&
      (bytes.length < 24 || bytes[0] !== 0xe9 || new DataView(bytes.buffer, bytes.byteOffset).getUint16(12, true) !== 9)) fail('Image is not ESP32-S3 firmware.');
  if (image.file === 'firmware.bin' && version !== undefined) {
    const field = offset => new TextDecoder().decode(bytes.subarray(offset, offset + 32)).split('\0')[0];
    if (bytes.length < 112 || new DataView(bytes.buffer, bytes.byteOffset).getUint32(32, true) !== 0xabcd5432 ||
        field(48) !== version || field(80) !== 'UberClicker') fail('Application descriptor version/project mismatch.');
  }
  return bytes;
}
export function checkDevice(chip, flashSize) {
  if (chip !== 'ESP32-S3' || flashSize !== '4MB') fail('Detected chip/flash is incompatible. Expected ESP32-S3 with exactly 4 MB.');
}
export async function verifyRelease(entry, fetchBytes) {
  if (!entry || !VERSION.test(entry.version) || entry.channel !== 'prerelease' || !/^[0-9a-f]{64}$/.test(entry.manifestSha256)) fail('Invalid release index entry.');
  const base = `releases/${entry.version}/`;
  const raw = await fetchBytes(base + 'manifest.json');
  if (await sha256(raw) !== entry.manifestSha256) fail('Manifest SHA-256 mismatch.');
  const manifest = validateManifest(JSON.parse(new TextDecoder().decode(raw)));
  if (manifest.version !== entry.version) fail('Release version mismatch.');
  const files = [];
  for (const image of manifest.images) files.push({ data: await validateImage(image, await fetchBytes(base + image.file), manifest.version), address: image.offset });
  return { manifest, files };
}
