"""Render the website's example TX screen with the actual firmware renderer."""
import base64
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

root = Path(__file__).resolve().parents[1]
assets = root / 'website/dist/assets'
with tempfile.TemporaryDirectory(prefix='uberclicker-hero-') as directory:
    work = Path(directory)
    source = work / 'hero.cpp'
    source.write_text('''#include "device_ui.h"
#include <cstdio>
#include <cstring>
int main() {
  device_ui::Snapshot snapshot;
  std::strcpy(snapshot.deviceName, "Joe Presenter");
  snapshot.battery = device_ui::Battery::High;
  snapshot.batteryPercent = 85;
  device_ui::Controller ui;
  ui.setSnapshot(snapshot);
  device_ui::Frame frame;
  ui.render(frame);
  return std::fwrite(frame.pixels, 1, sizeof(frame.pixels), stdout) == sizeof(frame.pixels) ? 0 : 1;
}
''')
    binary = work / 'hero'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'firmware/include'), str(root / 'firmware/src/device_ui.cpp'), str(source), '-o', str(binary)], check=True)
    bits = subprocess.check_output([str(binary)])
assert len(bits) == 5000
pixels = bytes(0 if bits[i // 8] & (128 >> (i % 8)) else 255 for i in range(40000))
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
rows = b''.join(b'\0' + pixels[y * 200:(y + 1) * 200] for y in range(200))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 200, 200, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
(assets / 'm5-tx-screen.png').write_bytes(png)
fallback = assets / 'device-fallback.svg'
svg, count = re.subn(r'data:image/png;base64,[A-Za-z0-9+/=]+', 'data:image/png;base64,' + base64.b64encode(png).decode(), fallback.read_text())
assert count == 1, 'The M5 fallback must embed exactly one TX screen'
fallback.write_text(svg)
print('Rendered Joe Presenter from the firmware into the 3D texture and SVG fallback.')
