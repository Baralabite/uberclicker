import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('package_release', Path(__file__).parents[1] / 'package-release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        parts = b''.join(struct.pack('<HBBII16sI', 0x50AA, kind, subtype, offset, size, label, 0) for kind, subtype, offset, size, label in [
            (1, 2, 0x9000, 0x5000, b'nvs'), (0, 0, 0x10000, 0x300000, b'app0')]) + b'\xff' * 32
        self.layout = {'chip': 'esp32s3', 'flashSize': '4MB', 'images': []}
        self.binary = bytearray(128)
        self.binary[0] = 0xe9
        self.binary[12] = 9
        struct.pack_into('<I', self.binary, 32, 0xABCD5432)
        self.binary[48:48+len(b'0.1.0-test.1\0')] = b'0.1.0-test.1\0'
        self.binary[80:91] = b'UberClicker'
        for offset, name, data in [(0, 'bootloader.bin', self.binary), (0x8000, 'partitions.bin', parts), (0x10000, 'firmware.bin', self.binary)]:
            source = self.root / name
            source.write_bytes(data)
            self.layout['images'].append({'offset': offset, 'path': str(source)})
    def package(self):
        return release.package(self.layout, self.root / 'out', '0.1.0-test.1', 'a' * 40)
    def test_exact_layout_hashes(self):
        manifest = self.package()
        self.assertEqual(manifest['images'][2]['offset'], 0x10000)
        self.assertEqual(manifest['images'][2]['sha256'], release.sha(self.binary))
        self.assertFalse(manifest['hardwareValidated'])
    def test_browser_accepts_package_and_rejects_corrupted_output(self):
        self.package()
        command = ['node', str(Path(__file__).parents[1] / 'check-package.mjs'), str(self.root / 'out')]
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
        (self.root / 'out/firmware.bin').write_bytes(b'corrupted')
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
    def test_wrong_chip(self):
        self.layout['chip'] = 'esp32'
        with self.assertRaises(ValueError): self.package()
    def test_overlap(self):
        self.layout['images'][2]['offset'] = 0x8000
        with self.assertRaises(ValueError): self.package()
    def test_bounds(self):
        self.layout['images'][2]['offset'] = 4194304
        with self.assertRaises(ValueError): self.package()
    def test_bad_partition(self):
        (self.root / 'partitions.bin').write_bytes(b'bad')
        with self.assertRaises(ValueError): self.package()
    def test_wrong_binary_chip(self):
        self.binary[12] = 0
        (self.root / 'firmware.bin').write_bytes(self.binary)
        with self.assertRaises(ValueError): self.package()
    def test_version_must_match_binary(self):
        with self.assertRaises(ValueError): release.package(self.layout, self.root / 'out', '0.1.0-other.1', 'a' * 40)
    def test_protocol_versions_are_separate_and_checked(self):
        header = 'kConfigSchemaVersion=2, kConfigProtocolVersion = 2;'
        metadata = release.protocol_metadata(header, 2, 'p[2]=1; p[2]=2;')
        self.assertEqual(metadata['bleFramingVersion'], 1)
        self.assertEqual(metadata['remoteConfigRadioVersion'], 2)
        self.assertEqual(metadata['preserveSettingsSchemas'], [1, 2])
        with self.assertRaises(ValueError): release.protocol_metadata(header, 1, 'p[2]=1;p[2]=2;')
        with self.assertRaises(ValueError): release.protocol_metadata(header, 2, 'p[2]=3;')
    def test_descriptor_must_identify_project(self):
        self.binary[80:91] = b'OtherDevice'
        (self.root / 'firmware.bin').write_bytes(self.binary)
        with self.assertRaises(ValueError): self.package()
    def test_descriptor_version_limit(self):
        with self.assertRaises(ValueError): release.package(self.layout, self.root / 'out', '0.1.0-' + 'a' * 26, 'a' * 40)
    def test_no_stable_publication(self):
        with self.assertRaises(ValueError): release.package(self.layout, self.root / 'out', '0.1.0', 'a' * 40)

if __name__ == '__main__': unittest.main()
