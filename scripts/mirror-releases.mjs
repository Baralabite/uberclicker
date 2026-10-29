// Reconstruct the Pages mirror from immutable published GitHub release assets.
import { execFileSync } from 'node:child_process';
import { mkdtemp, mkdir, readFile, writeFile, copyFile, rm } from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { VERSION, sha256, validateManifest, validateImage } from '../website/dist/flash/validate.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const destination = path.resolve(process.argv[2] || path.join(root, 'website/dist/flash/releases'));
const releases = JSON.parse(execFileSync('gh', ['api', '--paginate', '--slurp', 'repos/Baralabite/uberclicker/releases'], { encoding: 'utf8' })).flat();
const index = [];
const configHeader = await readFile(path.join(root, 'firmware/include/config_service.h'), 'utf8');
const settingsSchema = Number(configHeader.match(/kConfigSchemaVersion\s*=\s*(\d+)/)?.[1]);
const configProtocol = Number(configHeader.match(/kConfigProtocolVersion\s*=\s*(\d+)/)?.[1]);
if (![1, 2].includes(settingsSchema) || configProtocol !== settingsSchema) throw new Error('Review OTA client/schema compatibility before deploying');
// The USB partition CSV is also the OTA compatibility contract.
const types = { app: 0, data: 1, nvs: 2, ota: 0, ota_0: 16, ota_1: 17, coredump: 3 };
const otaPartitions = (await readFile(path.join(root, 'firmware/partitions.csv'), 'utf8')).split('\n')
  .filter(line => line.trim() && !line.trim().startsWith('#')).map(line => {
    const [, type, subtype, offset, size] = line.split(',').map(s => s.trim());
    return { type: types[type], subtype: types[subtype], offset: Number(offset), size: Number(size) };
  });
let otaManifest = {};
await mkdir(destination, { recursive: true });
for (const release of releases.filter(r => !r.draft && r.prerelease && VERSION.test(r.tag_name.replace(/^v/, ''))).sort((a, b) => b.published_at.localeCompare(a.published_at))) {
  const version = release.tag_name.replace(/^v/, '');
  const temp = await mkdtemp(path.join(os.tmpdir(), 'uc-mirror-'));
  try {
    const download = name => execFileSync('gh', ['release', 'download', release.tag_name, '--repo', 'Baralabite/uberclicker', '--pattern', name, '--dir', temp], { stdio: 'pipe' });
    download('manifest.json');
    const raw = await readFile(path.join(temp, 'manifest.json'));
    const manifest = validateManifest(JSON.parse(raw));
    if (manifest.version !== version) throw new Error('Release tag and manifest differ');
    for (const image of manifest.images) {
      download(image.file);
      await validateImage(image, new Uint8Array(await readFile(path.join(temp, image.file))), manifest.version);
    }
    const output = path.join(destination, version);
    await mkdir(output, { recursive: true });
    for (const name of ['manifest.json', ...manifest.images.map(i => i.file)]) {
      const source = await readFile(path.join(temp, name));
      let prior;
      try { prior = await readFile(path.join(output, name)); } catch (error) { if (error.code !== 'ENOENT') throw error; }
      if (prior && !prior.equals(source)) throw new Error(`Published bytes changed: ${version}/${name}`);
      await copyFile(path.join(temp, name), path.join(output, name));
      if (await sha256(await readFile(path.join(output, name))) !== await sha256(source)) throw new Error('Mirror hash mismatch');
    }
    const app = manifest.images.find(image => image.file === 'firmware.bin');
    if (!otaManifest.version && manifest.settingsSchemaVersion === settingsSchema && manifest.configProtocolVersion === configProtocol &&
        version.length <= 31 && app.offset === 0x10000 && app.size <= 0x1f0000 &&
        manifest.partitions.length === otaPartitions.length && otaPartitions.every(expected =>
          manifest.partitions.filter(p => Object.entries(expected).every(([key, value]) => p[key] === value)).length === 1)) {
      otaManifest = manifest;
    }
    index.push({ version, channel: 'prerelease', manifestSha256: await sha256(raw), publishedAt: release.published_at, releaseUrl: release.html_url });
    console.log(`Verified exact GitHub/Pages bytes: ${version}`);
  } finally { await rm(temp, { recursive: true, force: true }); }
}
await writeFile(path.join(destination, 'index.json'), JSON.stringify({ indexVersion: 1, releases: index }, null, 2) + '\n');
await writeFile(path.join(destination, 'ota.json'), JSON.stringify(otaManifest, null, 2) + '\n');
console.log(`Mirrored ${index.length} published prereleases; no stable release is available before hardware acceptance.`);
