"""Refresh the device design inside its checked-in standalone runtime shell."""

from html import escape
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / "designs" / "device-interface.html"
preview = root / "device-preview.html"
start = "&lt;!-- DEVICE_INTERFACE_START --&gt;\n"
end = "&lt;!-- DEVICE_INTERFACE_END --&gt;"
wrapper = preview.read_text()

if wrapper.count(start) != 1 or wrapper.count(end) != 1:
    raise SystemExit("Preview must contain exactly one device interface marker pair")

prefix, content = wrapper.split(start, 1)
_, suffix = content.split(end, 1)
preview.write_text(prefix + start + escape(source.read_text(), quote=True) + end + suffix)
print("Updated device-preview.html from designs/device-interface.html")
