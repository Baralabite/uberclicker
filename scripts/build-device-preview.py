"""Compile the real UI and synchronize review evidence. --check writes nothing."""
import argparse
import base64
import html
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = root / "designs/device-interface.html"
preview = root / "device-preview.html"
assets = root / "designs/firmware"
expected = {}

def png(pixels, width=200, height=200):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + pixels[y*width:(y+1)*width] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

frames, raw = {}, {}
with tempfile.TemporaryDirectory(prefix="uberclicker-ui-") as work:
    work = Path(work)
    binary = work / "ui-test"
    rendered = work / "frames"
    rendered.mkdir()
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "firmware/include"),
                    str(root / "firmware/src/device_ui.cpp"),
                    str(root / "firmware/tests/ui/device_ui_test.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(rendered)], check=True)
    adapter = work / "display-test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DARDUINO",
                    "-I" + str(root / "firmware/tests/ui/stubs"),
                    "-I" + str(root / "firmware/include"),
                    str(root / "firmware/src/device_ui.cpp"),
                    str(root / "firmware/tests/ui/display_adapter_test.cpp"),
                    "-o", str(adapter)], check=True)
    subprocess.run([str(adapter)], check=True)

    for path in sorted(rendered.glob("*.pbm")):
        data = path.read_bytes()
        assert data.startswith(b"P4\n200 200\n") and len(data) == 5011, path
        bits = data[11:]
        frames[path.stem] = base64.b64encode(bits).decode()
        raw[path.stem] = bytes(0 if bits[i//8] & (128 >> (i%8)) else 255 for i in range(40000))
        expected[assets / path.name] = data
        expected[(assets / path.name).with_suffix(".png")] = png(raw[path.stem])
# TX home/unknown, RX home/editing, reset Cancel/armed. Source pixels unchanged.
sheet = bytearray([255] * (424*636))
for index, name in enumerate(["tx-home", "tx-unknown", "rx-home", "editing", "reset-cancel", "reset-armed"]):
    left = 8 + (index%2)*208
    top = 8 + (index//2)*208
    for y in range(200):
        sheet[(top+y)*424+left:(top+y)*424+left+200] = raw[name][y*200:(y+1)*200]
expected[assets / "review-sheet.png"] = png(sheet, 424, 636)

tx_names = ["tx-all", "tx-partial", "tx-unknown", "menu-0", "receivers-mixed", "receivers-1"]
tx_sheet = bytearray([255] * (424*636))
for index, name in enumerate(tx_names):
    left, top = 8 + (index%2)*208, 8 + (index//2)*208
    for y in range(200):
        tx_sheet[(top+y)*424+left:(top+y)*424+left+200] = raw[name][y*200:(y+1)*200]
expected[assets / "tx-review-sheet.png"] = png(tx_sheet, 424, 636)


remote_names = ["remote-right-pending", "remote-left-pending", "remote-right-confirmed", "remote-right-unknown",
                "remote-right-cached", "remote-right-unreachable", "remote-right-editing", "rx-managed"]
remote_sheet = bytearray([255] * (424*844))
for index, name in enumerate(remote_names):
    left, top = 8 + (index%2)*208, 8 + (index//2)*208
    for y in range(200):
        remote_sheet[(top+y)*424+left:(top+y)*424+left+200] = raw[name][y*200:(y+1)*200]
expected[assets / "remote-review-sheet.png"] = png(remote_sheet, 424, 844)

recovery_names = ["result-right-pending", "result-right-found", "result-right-unknown",
                  "recovery-home", "recovery-menu-0", "recovery-reset-cancel"]
recovery_sheet = bytearray([255] * (424*636))
for index, name in enumerate(recovery_names):
    left, top = 8 + (index%2)*208, 8 + (index//2)*208
    for y in range(200):
        recovery_sheet[(top+y)*424+left:(top+y)*424+left+200] = raw[name][y*200:(y+1)*200]
expected[assets / "recovery-review-sheet.png"] = png(recovery_sheet, 424, 636)



start, end = "// FIRMWARE_FRAMES_START\n", "// FIRMWARE_FRAMES_END"
authored = source.read_text()
assert authored.count(start) == authored.count(end) == 1
prefix, rest = authored.split(start)
_, suffix = rest.split(end)
authored = prefix + start + "const frames=" + json.dumps(frames, separators=(",", ":")) + ";\n" + end + suffix
expected[source] = authored.encode()
start, end = "&lt;!-- DEVICE_INTERFACE_START --&gt;\n", "&lt;!-- DEVICE_INTERFACE_END --&gt;"
wrapper = preview.read_text()
assert wrapper.count(start) == wrapper.count(end) == 1
prefix, rest = wrapper.split(start)
_, suffix = rest.split(end)
expected[preview] = (prefix + start + html.escape(authored, quote=True) + end + suffix).encode()

stale = [path for path, data in expected.items() if not path.exists() or path.read_bytes() != data]
obsolete = [path for path in assets.iterdir() if path.suffix in (".pbm", ".png") and path not in expected] if assets.exists() else []
if args.check:
    if stale or obsolete:
        stale += obsolete
        raise SystemExit("Outdated review files: " + ", ".join(str(path.relative_to(root)) for path in stale))
    print(f"Verified {len(frames)} firmware frames, PNGs, authored fragment and standalone preview")
else:
    for path in obsolete:
        path.unlink()
    for path, data in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"Updated {len(frames)} real 200x200 frames and synchronized device-preview.html")
