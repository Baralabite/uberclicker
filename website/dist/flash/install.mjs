import { ESPLoader, Transport } from './vendor/esptool-js-0.7.0.js';
import { BOARD, verifyRelease } from './validate.mjs';
import { FlashSession } from './session.mjs';

const $ = id => document.getElementById(id);
// Only the same-origin Companion parent supplies this narrow native bridge.
let native;
try { if (window.parent.location.origin === location.origin) native = window.parent.uberclickerInstaller; } catch {}
const supported = !!crypto.subtle && (!!native || (isSecureContext && 'serial' in navigator));
let entries = [], verified, session, busy = false, writing = false;
const status = message => { $('status').textContent = message; };
function log(message) { $('log').textContent = ($('log').textContent + message + '\n').slice(-16000); }
function controls() {
  $('connect').disabled = !supported || busy || !!session || $('board').value !== BOARD || !$('release').value || ($('mode').value === 'erase' && !$('erase-consent').checked);
  $('write').disabled = busy || !session?.ready;
  $('disconnect').disabled = busy || !session;
  for (const id of ['board', 'channel', 'release', 'mode', 'erase-consent']) $(id).disabled = busy || !!session || (id === 'release' && !entries.some(e => e.channel === $('channel').value));
  $('port').disabled = $('refresh-ports').disabled = busy || !!session;
  $('refresh-releases').disabled = busy || !!session;
  if (native && !$('port').value) $('connect').disabled = true;
}
async function bytes(url) {
  if (native) return new Uint8Array(await native.bytes(url));
  const response = await fetch(url, { cache: 'no-cache' });
  if (!response.ok) throw new Error(`Download failed (${response.status}): ${url}`);
  const size = Number(response.headers.get('Content-Length'));
  if (size > 4194304) throw new Error('Download exceeds maximum image size.');
  const body = new Uint8Array(await response.arrayBuffer());
  if (body.length > 4194304) throw new Error('Download exceeds maximum image size.');
  return body;
}
function versions() {
  const available = entries.filter(e => e.channel === $('channel').value);
  $('release').replaceChildren(...available.map(e => new Option(e.version, e.version)));
  if (!available.length) $('release').add(new Option('No published releases in this channel', ''));
  $('release-detail').textContent = available.length ? 'Choose a firmware version to install.' : 'No published releases in this channel. Choose another channel.';
  controls();
}
async function close() {
  if (session) { const old = session; session = undefined; try { await old.disconnect(); } catch (error) { log(error.message); } }
  verified = undefined;
  controls();
}
$('support').textContent = native ? 'Native USB flashing available. Disconnect the device in Configure before connecting here.' : supported ? 'Web Serial available. A port opens only after you choose Connect.' : 'This browser cannot flash over Web Serial. Use desktop Chrome or Edge on HTTPS, or the command-line recovery instructions. iPhone/iPad browsers are unsupported.';
if (native) {
  $('native-ports').hidden = false;
  $('prepare-help').textContent = 'Connect your ThinkNode M5 with a USB data cable. Close other serial apps. After installing, use Configure to select Transmitter or Receiver.';
  $('channel').value = 'prerelease';
  const refresh = async () => {
    busy = true; controls();
    try {
      const ports = await native.ports();
      $('port').replaceChildren(new Option('Select a USB device…', ''), ...ports.map(port => new Option(port.label, port.path)));
      status(ports.length ? 'Choose the USB device to flash. Nothing has been written.' : 'No USB devices found. Check the data cable and refresh.');
    } catch (error) { status(`USB discovery failed: ${error.message ?? error}`); }
    finally { busy = false; controls(); }
  };
  $('refresh-ports').addEventListener('click', refresh);
  $('port').addEventListener('change', controls);
  await refresh();
}
$('channel').addEventListener('change', versions);
for (const id of ['board', 'release', 'mode', 'erase-consent']) $(id).addEventListener('change', controls);
$('connect').addEventListener('click', async () => {
  busy = true; controls(); $('progress').value = 0;
  try {
    // Request permission in the click gesture; do not open the port until validation.
    const port = native ? native.port($('port').value) : await navigator.serial.requestPort();
    status('Downloading and verifying manifest, chip headers and every image…');
    verified = await verifyRelease(entries.find(e => e.version === $('release').value), bytes);
    status('Downloads verified. Connecting resets the device into its bootloader.');
    const transport = new Transport(port, true);
    const loader = new ESPLoader({ transport, baudrate: 115200, terminal: { clean() {}, write: log, writeLine: log }, debugLogging: false });
    session = new FlashSession(verified, loader, transport);
    await session.connect($('mode').value, $('erase-consent').checked);
    status(`ESP32-S3 / 4 MB verified for ${verified.manifest.version}. Ready. Press Write firmware to begin. Nothing written yet.`);
  } catch (error) {
    status(error.name === 'NotFoundError' ? 'Port selection cancelled. Nothing written.' : `${error.message ?? error} Reconnect with a data cable; close other serial apps. No automatic erase or retry.`);
    await close();
  } finally { busy = false; controls(); }
});
$('write').addEventListener('click', async () => {
  busy = writing = true; controls();
  const active = session;
  try {
    status('Writing firmware. Keep power and cable connected…');
    await active.write((index, written, total) => {
      $('progress').value = ((index + written / total) / verified.files.length) * 100;
      status(`Writing image ${index + 1}/${verified.files.length}: ${Math.floor(written / total * 100)}%. Read-back verification follows.`);
    });
    $('progress').value = 100;
    status('Write and SHA-256 read-back verification complete. Device reset requested.');
  } catch (error) { status(`Write failed: ${error.message ?? error} Keep the device powered, reconnect in bootloader mode and reinstall. No automatic retry.`); }
  finally { writing = false; await close(); busy = false; controls(); }
});
$('disconnect').addEventListener('click', async () => { await close(); status('Disconnected. Reconnect or power-cycle to leave the bootloader.'); });
if (!native && 'serial' in navigator) navigator.serial.addEventListener('disconnect', () => {
  session?.invalidate();
  if (!writing) { void close(); status('Serial device disconnected. Reconnect to verify compatibility again.'); }
  controls();
});
async function loadReleases() {
  busy = true; controls();
  $('release-detail').textContent = 'Loading published release index…';
  try {
    const index = JSON.parse(new TextDecoder().decode(await bytes('releases/index.json')));
    if (index.indexVersion !== 1 || !Array.isArray(index.releases)) throw new Error('Unsupported release index.');
    entries = index.releases;
    versions();
  } catch (error) {
    entries = []; versions();
    $('release-detail').textContent = `Release index unavailable: ${error.message ?? error}. Choose Refresh releases to retry.`;
  } finally { busy = false; controls(); }
}
$('refresh-releases').addEventListener('click', loadReleases);
await loadReleases();
