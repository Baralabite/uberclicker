#!/usr/bin/env python3
"""Compile the production OTA worker against deterministic HTTP/partition boundaries."""
import json
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
firmware = root / 'firmware'
json_headers = firmware / '.pio/libdeps/thinknode_m5/ArduinoJson/src'
openssl = Path(os.environ.get('OPENSSL_PREFIX', '/opt/homebrew/opt/openssl@3'))
flags = ['-I'+str(openssl/'include'), '-L'+str(openssl/'lib')] if openssl.exists() else []
types = dict(app=0, data=1, nvs=2, ota=0, ota_0=16, ota_1=17, coredump=3)
partitions = []
for row in (firmware / 'partitions.csv').read_text().splitlines():
    if not row.strip() or row.startswith('#'): continue
    name, kind, subtype, offset, size, *_ = [s.strip() for s in row.split(',')]
    partitions.append(dict(name=name, type=types[kind], subtype=types[subtype], offset=int(offset, 0), size=int(size, 0)))
versions = (firmware / 'include/config_service.h').read_text()
schema = int(re.search(r'kConfigSchemaVersion\s*=\s*(\d+)', versions)[1])
protocol = int(re.search(r'kConfigProtocolVersion\s*=\s*(\d+)', versions)[1])
manifest = dict(manifestVersion=1, version='0.2.0-test.1', board='thinknode_m5', chip='esp32s3',
                flashSize=4194304, channel='prerelease', sourceDirty=False, settingsSchemaVersion=schema,
                configProtocolVersion=protocol, radioProtocolVersion=1, partitions=partitions,
                images=[dict(file='firmware.bin', offset=65536, size=8192, sha256='0'*64)])
with tempfile.TemporaryDirectory(prefix='uc-ota-tests-') as tmp:
    (Path(tmp) / 'config_service.h').write_text(f'namespace uc {{ constexpr unsigned kConfigSchemaVersion={schema}, kConfigProtocolVersion={protocol}; }}')
    fixture = Path(tmp) / 'manifest.json'
    fixture.write_text(json.dumps(manifest))
    binary = Path(tmp) / 'runtime'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-DUBERCLICKER_VERSION="0.1.0-test.1"',
        '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0', '-DARDUINOJSON_ENABLE_PROGMEM=0',
        '-I'+tmp, '-I'+str(firmware/'tests/ota/stubs'), '-I'+str(firmware/'include'), '-I'+str(json_headers),
        str(firmware/'src/ota_update.cpp'), str(firmware/'tests/ota/runtime.cpp'), *flags, '-lcrypto', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(fixture)], check=True)
