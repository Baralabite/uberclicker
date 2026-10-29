#!/usr/bin/env python3
"""Build a public experimental candidate and package exact upload images (no device I/O)."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*")

def protocol_metadata(header, settings_schema, radio_source):
    values = {}
    for name, field in [('kConfigSchemaVersion', 'settingsSchemaVersion'), ('kConfigProtocolVersion', 'configProtocolVersion')]:
        match = re.search(name + r'\s*=\s*(\d+)', header)
        if not match: raise ValueError('Missing firmware configuration version')
        values[field] = int(match[1])
    schema = values['settingsSchemaVersion']
    if schema not in (1, 2) or values['configProtocolVersion'] != schema or settings_schema != schema:
        raise ValueError('Review firmware/settings/client protocol compatibility before packaging')
    wire_versions = {int(n) for n in re.findall(r'p\[\s*2\s*\]\s*=\s*(\d+)\s*;', radio_source)}
    if wire_versions != ({1, 2} if schema == 2 else {1}):
        raise ValueError('Review cue and encrypted-management radio versions against the source')
    return {**values, 'radioProtocolVersion': 1, 'bleFramingVersion': 1,
            'remoteConfigRadioVersion': 2 if schema == 2 else 0,
            'preserveSettingsSchemas': [1, 2] if schema == 2 else [1],
            'settingsMigration': 'atomic-on-apply-v1-to-v2' if schema == 2 else 'none'}

def source_bundle(layout, output):
    """Convey the project and the installed library/core sources with binaries."""
    files = subprocess.check_output(['git', 'ls-files', '-co', '--exclude-standard', '--',
        'firmware', 'contracts', 'scripts', '.github', 'LICENSE', 'licenses', 'README.md', 'docs'], cwd=ROOT, text=True).splitlines()
    with zipfile.ZipFile(output / 'firmware-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(set(files)):
            source = ROOT / name
            if source.is_file(): archive.write(source, 'uberclicker/' + name)
        for key, prefix in [('librarySource', 'libraries'), ('frameworkSource', 'arduino-core')]:
            directory = Path(layout[key])
            if not directory.is_dir(): raise ValueError('Missing installed dependency source')
            for source in sorted(directory.rglob('*')):
                if source.is_file() and '.git' not in source.parts:
                    archive.write(source, 'dependencies/' + prefix + '/' + str(source.relative_to(directory)))
        sdk = Path(layout['sdkPackage'])
        for name in ['package.json', 'versions.txt', 'esp32s3/dependencies.lock', 'esp32s3/sdkconfig', 'esp32s3/versions.txt', 'esp32s3/pioarduino-build.py']:
            archive.write(sdk / name, 'dependencies/sdk-build-metadata/' + name)
    (output / 'THIRD_PARTY_NOTICES.md').write_text((ROOT / 'docs/dependencies.md').read_text())
    (output / 'COPYING-GPL-3.txt').write_bytes((Path(layout['librarySource']) / 'GxEPD2/LICENSE').read_bytes())
    (output / 'COPYING-LGPL-2.1.txt').write_bytes((ROOT / 'licenses/LGPL-2.1.txt').read_bytes())
    # Checksums cover the source and licenses as well as every flash image.
    (output / 'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(output.iterdir()) if p.name != 'SHA256SUMS'))

def sha(data):
    return hashlib.sha256(data).hexdigest()

def package(layout, output, version, commit, source_dirty=False):
    if len(version) > 31 or not VERSION.fullmatch(version) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Use a semantic prerelease version and a full source commit")
    if layout["chip"] != "esp32s3" or layout["flashSize"] != "4MB":
        raise ValueError("Only the ThinkNode M5 ESP32-S3 4MB build is supported")
    images = []
    sources = {}
    for entry in sorted(layout["images"], key=lambda e: e["offset"]):
        source = Path(entry["path"])
        data = source.read_bytes()
        offset = entry["offset"]
        if not data or offset < 0 or offset % 4096 or offset + len(data) > 4194304:
            raise ValueError("Empty, unaligned or out-of-bounds image")
        if images and offset < images[-1]["offset"] + ((images[-1]["size"] + 4095) // 4096) * 4096:
            raise ValueError("Overlapping image erase sectors")
        name = source.name
        if name in sources or not re.fullmatch(r"[A-Za-z0-9_.-]+\.bin", name):
            raise ValueError("Duplicate or invalid image filename")
        if name in ("bootloader.bin", "firmware.bin") and (len(data) < 24 or data[0] != 0xE9 or struct.unpack_from('<H', data, 12)[0] != 9):
            raise ValueError("Not an ESP32-S3 firmware image")
        images.append({"file": name, "offset": offset, "size": len(data), "sha256": sha(data)})
        sources[name] = data
    if not {"bootloader.bin", "partitions.bin", "firmware.bin"}.issubset(sources):
        raise ValueError("Missing bootloader, partitions or firmware")
    if (len(sources['firmware.bin']) < 112 or struct.unpack_from('<I', sources['firmware.bin'], 32)[0] != 0xABCD5432
            or sources['firmware.bin'][48:80].split(b'\0')[0] != version.encode()
            or sources['firmware.bin'][80:112].split(b'\0')[0] != b'UberClicker'):
        raise ValueError('Application descriptor does not match the packaged version/project')
    # The partition table is a binary build output, not an assumed board layout.
    partitions = []
    for pos in range(0, len(sources["partitions.bin"]) - 31, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from("<HBBII16sI", sources["partitions.bin"], pos)
        if magic != 0x50AA:
            break
        if not size or offset + size > 4194304:
            raise ValueError("Invalid partition bounds")
        partitions.append({"name": label.split(b"\0")[0].decode("ascii"), "type": kind,
                           "subtype": subtype, "offset": offset, "size": size})
    settings = [p for p in partitions if p["type"] == 1 and p["subtype"] == 2]
    ordered = sorted(partitions, key=lambda p: p["offset"])
    if any(p["offset"] % 4096 or (i and ordered[i - 1]["offset"] + ordered[i - 1]["size"] > p["offset"]) for i, p in enumerate(ordered)):
        raise ValueError("Overlapping or unaligned partitions")
    apps = [p for p in partitions if p["type"] == 0]
    application = next(i for i in images if i["file"] == "firmware.bin")
    if not settings or not any(p["offset"] == application["offset"] and application["size"] <= p["size"] for p in apps):
        raise ValueError("Missing NVS settings or application does not fit partition")
    for p in settings:
        if any(i["offset"] < p["offset"] + p["size"] and i["offset"] + ((i["size"] + 4095) // 4096) * 4096 > p["offset"] for i in images):
            raise ValueError("Image erase would overwrite settings")
    versions = protocol_metadata((ROOT / 'firmware/include/config_service.h').read_text(),
        json.loads((ROOT / 'contracts/config.schema.json').read_text())['properties']['schemaVersion']['const'],
        (ROOT / 'firmware/src/radio_protocol.cpp').read_text())
    manifest = {"manifestVersion": 1, "version": version, "channel": "prerelease",
                "board": "thinknode_m5", "chip": "esp32s3", "flashSize": 4194304,
                "sourceCommit": commit, "sourceDirty": source_dirty, **versions,
                "hardwareValidated": False, "images": images, "partitions": partitions}
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    for name, data in sources.items():
        (output / name).write_bytes(data)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(f"{sha(p.read_bytes())}  {p.name}\n" for p in sorted(output.iterdir())))
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-dirty", action="store_true", help="Local dry run only; its manifest cannot be published")
    args = parser.parse_args()
    if len(args.version) > 31 or not VERSION.fullmatch(args.version):
        parser.error("Use an explicit prerelease version of at most 31 characters until hardware acceptance")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())
    if dirty and not args.allow_dirty:
        parser.error("Source is dirty; commit first, or explicitly use --allow-dirty for a local-only dry run")
    conf = configparser.ConfigParser(interpolation=None)
    conf.read(ROOT / "firmware/platformio.ini")
    section = conf["env:thinknode_m5"]
    flags = re.sub(r"\s*-D\s+UBERCLICKER_VERSION=\S+", "", section.get("build_flags", ""))
    section["build_flags"] = flags + '\n-D UBERCLICKER_VERSION=\\"' + args.version + '\\"'
    section["extra_scripts"] = section.get("extra_scripts", "") + "\npost:" + str(ROOT / "scripts/export-flash-layout.py")
    with tempfile.TemporaryDirectory(prefix="uc-release-") as temp:
        if not conf.has_section("platformio"):
            conf.add_section("platformio")
        conf["platformio"]["build_dir"] = str(Path(temp) / "build")
        cfg = Path(temp) / "platformio.ini"
        with cfg.open("w") as handle:
            conf.write(handle)
        subprocess.run(["pio", "run", "-d", str(ROOT / "firmware"), "-c", str(cfg), "-e", "thinknode_m5"], check=True)
        layout = json.loads((Path(temp) / "build/thinknode_m5/flash-layout.json").read_text())
        package(layout, args.output, args.version, commit, dirty)
        source_bundle(layout, args.output)

if __name__ == "__main__":
    main()
