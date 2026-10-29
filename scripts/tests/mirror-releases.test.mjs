import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, readFile, rm, chmod } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { sha256 } from '../../website/dist/flash/validate.mjs';

test('mirror retains versions, selects compatible OTA, and rejects changed/corrupt bytes', async () => {
  const temporary = await mkdtemp(path.join(os.tmpdir(), 'uc-mirror-test-'));
  try {
    const bin = path.join(temporary, 'bin'); await mkdir(bin);
    const gh = path.join(bin, 'gh');
    await writeFile(gh, `#!/usr/bin/env node
const fs=require('node:fs'),path=require('node:path');const args=process.argv.slice(2),root=process.env.UC_RELEASE_FIXTURE;
if(args[0]==='api')process.stdout.write(JSON.stringify([JSON.parse(fs.readFileSync(path.join(root,'list.json')))]));
else if(args[0]==='release'&&args[1]==='download'){const tag=args[2],name=args[args.indexOf('--pattern')+1],out=args[args.indexOf('--dir')+1];fs.copyFileSync(path.join(root,tag,name),path.join(out,name));}
else process.exit(1);
`); await chmod(gh, 0o755);
    const releases = [];
    for (const number of [1, 2, 3, 4]) {
      const version = `0.1.0-test.${number}`, tag = 'v' + version, directory = path.join(temporary, tag);
      await mkdir(directory);
      const bytes = new Uint8Array(128); bytes[0] = 0xe9; bytes[12] = 9; bytes[30] = number;
      new DataView(bytes.buffer).setUint32(32, 0xabcd5432, true);
      bytes.set(new TextEncoder().encode(version), 48); bytes.set(new TextEncoder().encode('UberClicker'), 80);
      const images = [];
      for (const [file, offset] of [['bootloader.bin', 0], ['partitions.bin', 0x8000], ['firmware.bin', 0x10000]]) {
        await writeFile(path.join(directory, file), bytes);
        images.push({ file, offset, size: bytes.length, sha256: await sha256(bytes) });
      }
      await writeFile(path.join(directory, 'manifest.json'), JSON.stringify({ manifestVersion: 1, version, channel: 'prerelease', board: 'thinknode_m5', chip: 'esp32s3', flashSize: 4194304, sourceCommit: 'a'.repeat(40), sourceDirty: false, hardwareValidated: false, settingsSchemaVersion: number === 2 ? 2 : 1, configProtocolVersion: number === 2 ? 2 : 1, radioProtocolVersion: 1, ...(number === 2 ? {bleFramingVersion: 1, remoteConfigRadioVersion: 2, preserveSettingsSchemas: [1, 2], settingsMigration: 'atomic-on-apply-v1-to-v2'} : {}), images,
        partitions: number !== 3 ? [{ name: 'nvs', type: 1, subtype: 2, offset: 0x9000, size: 0x5000 }, { name: 'otadata', type: 1, subtype: 0, offset: 0xe000, size: 0x2000 }, { name: 'app0', type: 0, subtype: 16, offset: 0x10000, size: 0x1f0000 }, { name: 'app1', type: 0, subtype: 17, offset: 0x200000, size: 0x1f0000 }, { name: 'coredump', type: 1, subtype: 3, offset: 0x3f0000, size: 0x10000 }] : [{ name: 'nvs', type: 1, subtype: 2, offset: 0x9000, size: 0x5000 }, { name: 'app', type: 0, subtype: 0, offset: 0x10000, size: 0x300000 }] }));
      releases.push({ draft: false, prerelease: true, tag_name: tag, published_at: `2026-10-29T01:00:0${number}Z`, html_url: 'https://github.com/Baralabite/uberclicker/releases/tag/' + tag });
    }
    await writeFile(path.join(temporary, 'list.json'), JSON.stringify(releases));
    const destination = path.join(temporary, 'mirror');
    const script = fileURLToPath(new URL('../mirror-releases.mjs', import.meta.url));
    const run = () => execFileSync(process.execPath, [script, destination], { env: { ...process.env, PATH: bin + path.delimiter + process.env.PATH, UC_RELEASE_FIXTURE: temporary }, stdio: 'pipe' });
    run();
    const index = JSON.parse(await readFile(path.join(destination, 'index.json')));
    assert.deepEqual(index.releases.map(r => r.version), ['0.1.0-test.4', '0.1.0-test.3', '0.1.0-test.2', '0.1.0-test.1']);
    const feed = JSON.parse(await readFile(path.join(destination, 'ota.json')));
    assert.equal(feed.version, '0.1.0-test.2'); // Newer incompatible schema and single-slot releases cannot replace an OTA candidate.
    assert.deepEqual(feed, JSON.parse(await readFile(path.join(temporary, 'v0.1.0-test.2/manifest.json'))));
    for (const entry of index.releases) {
      const original = path.join(temporary, 'v' + entry.version);
      const mirror = path.join(destination, entry.version);
      assert.equal(await sha256(await readFile(path.join(mirror, 'manifest.json'))), entry.manifestSha256);
      for (const name of ['manifest.json', 'bootloader.bin', 'partitions.bin', 'firmware.bin']) assert.deepEqual(await readFile(path.join(mirror, name)), await readFile(path.join(original, name)));
    }
    const image = path.join(destination, '0.1.0-test.2', 'firmware.bin');
    const original = await readFile(image); await writeFile(image, 'changed');
    assert.throws(run, /Published bytes changed/);
    await writeFile(image, original);
    await writeFile(path.join(temporary, 'v0.1.0-test.2', 'firmware.bin'), 'corrupt');
    assert.throws(run, /Size or SHA-256 mismatch/);
    await writeFile(path.join(temporary, 'list.json'), JSON.stringify([releases[2]]));
    run();
    assert.deepEqual(JSON.parse(await readFile(path.join(destination, 'ota.json'))), {}); // Withdraw stale candidate.

  } finally { await rm(temporary, { recursive: true, force: true }); }
});
