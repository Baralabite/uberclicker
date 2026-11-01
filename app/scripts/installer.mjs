import assert from 'node:assert/strict';
import { rm, cp, copyFile, mkdir, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const app = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = path.resolve(app, '../website/dist');
const target = path.join(app, 'public/installer');
await rm(target, { recursive: true, force: true });
await cp(path.join(source, 'flash'), target, { recursive: true });
await mkdir(path.join(target, 'assets'), { recursive: true });
for (const file of ['barlow-regular.ttf', 'barlow-semibold.ttf', 'barlow-condensed-bold.ttf', 'Barlow-OFL.txt', 'Barlow-Condensed-OFL.txt'])
  await copyFile(path.join(source, 'assets', file), path.join(target, 'assets', file));
await copyFile(path.join(source, 'style.css'), path.join(target, 'website.css'));
let html = await readFile(path.join(target, 'index.html'), 'utf8');
assert(html.includes('href="../style.css"') && html.includes('<h1>Install firmware</h1>'), 'Update installer embedding for the changed source template');
html = html.replace(/<header[\s\S]*?<\/header>/, '').replace('<h1>Install firmware</h1>', '')
  .replace('href="../style.css"', 'href="website.css"').replace(/<link rel="icon"[^>]+>/g, '')
  .replace('</head>', '<link rel="stylesheet" href="embed.css"></head>')
  .replace('</body>', '<script type="module" src="embed.mjs"></script></body>');
await writeFile(path.join(target, 'index.html'), html);
await writeFile(path.join(target, 'embed.css'), 'body{font-family:"Avenir Next",Avenir,"Segoe UI",sans-serif;font-size:16px}.installer{max-width:none;width:100%;padding:0 4px 24px}.installer h2{font:700 22px/1.2 "Avenir Next",Avenir,"Segoe UI",sans-serif}.installer .lead{font-size:16px}.installer .lead{margin-top:0}');
await writeFile(path.join(target, 'embed.mjs'), `new ResizeObserver(() => parent.postMessage({type:'uberclicker:installer-size',height:document.body.offsetHeight},location.origin)).observe(document.body);\n`);
console.log('Prepared the existing verified USB installer for the web app and Companion.');
