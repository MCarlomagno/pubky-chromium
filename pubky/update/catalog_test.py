#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import base64
import copy
import datetime as dt
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import catalog

ROOT = Path(__file__).resolve().parent
FIXTURES = ROOT / 'testdata'
CONTEXT = dict(target='linux', running='156.0.8073.0', installed='156.0.8073.0',
               floor='', now=catalog.timestamp('2026-10-05T18:00:00Z'))


class CatalogTest(unittest.TestCase):
    def setUp(self):
        self.payload = (FIXTURES / 'catalog.json').read_bytes()
        self.record = json.loads(self.payload)
        self.key = catalog.decode((FIXTURES / 'public-key.b64').read_text().strip(), 32)
        self.envelope = (FIXTURES / 'linux-x64.json').read_bytes()

    def test_public_fixture_domain_signature_and_canonical_bytes(self):
        self.assertEqual(catalog.canonical(self.record), self.payload)
        self.assertEqual(catalog.DOMAIN + self.payload, (FIXTURES / 'catalog.bin').read_bytes())
        self.assertEqual(self.record, catalog.verify_envelope(self.envelope, self.key, **CONTEXT))
        outer = json.loads(self.envelope)
        outer['payload'] = base64.b64encode(self.payload + b' ').decode()
        with self.assertRaises(ValueError):
            catalog.verify_envelope(catalog.canonical(outer), self.key, **CONTEXT)
        with self.assertRaises(ValueError):
            catalog.verify_envelope(self.envelope, bytes(32), **CONTEXT)
        signature = catalog.decode(outer['signature'], 64)
        with self.assertRaises(ValueError):
            catalog.verify_signature(b'other-domain\n' + self.payload, signature, self.key)

    def test_closed_schema_and_bad_input(self):
        for field, value in [('schema', True), ('size', True), ('size', 0),
                             ('size', 2147483648), ('command', 'run'),
                             ('product', 'Pubky Chromium\n'), ('product', 'Pubky <Chromium>'),
                             ('source_revision', 'a'*39), ('sha256', 'A'*64),
                             ('tag', '..'), ('asset', 'other.zip')]:
            with self.subTest(field=field, value=value):
                record = copy.deepcopy(self.record)
                record[field] = value
                with self.assertRaises(ValueError):
                    catalog.validate(record, **CONTEXT)
        for data in [self.envelope + b'\n', self.envelope[:100],
                     b'x'*(catalog.MAX_ENVELOPE+1),
                     b'{"size":1,"size":2}']:
            with self.assertRaises((ValueError, json.JSONDecodeError)):
                catalog.verify_envelope(data, self.key, **CONTEXT)
        for text in ['AA', 'AB==', 'AAAA\n', '!!!=', 123]:
            with self.assertRaises(ValueError):
                catalog.decode(text)

    def test_identity_url_versions_and_time(self):
        for field in ['repository', 'platform', 'architecture', 'channel', 'package_id']:
            record = copy.deepcopy(self.record)
            record[field] = 'wrong'
            with self.assertRaises(ValueError):
                catalog.validate(record, **CONTEXT)
        for field, value in [('url', 'https://evil.example/pubky.deb'),
                             ('url', self.record['url'] + '?x=1'),
                             ('url', self.record['url'].replace('https:', 'http:')),
                             ('product_version', '156.0.8073.01'),
                             ('native_version', '156.0.8073.2-1'),
                             ('expires_at', '2026-10-05T18:00:00Z'),
                             ('expires_at', '2027-10-05T18:00:00Z'),
                             ('issued_at', '2026-10-05T18:06:00Z'),
                             ('issued_at', '2026-02-30T18:00:00Z')]:
            record = copy.deepcopy(self.record)
            record[field] = value
            with self.assertRaises(ValueError):
                catalog.validate(record, **CONTEXT)
        for kwargs in [dict(running='156.0.8073.1'), dict(installed='156.0.8073.1'),
                       dict(floor='156.0.8073.2')]:
            with self.assertRaises(ValueError):
                catalog.validate(self.record, **(CONTEXT | kwargs))
        catalog.validate(self.record, **(CONTEXT | dict(floor='156.0.8073.1')))
        windows = copy.deepcopy(self.record)
        windows.update(platform='windows', package_id='PubkyChromium', native_version='156.0.8073.1',
                       asset='mini_installer.exe', url=self.record['url'].replace('pubky.deb', 'mini_installer.exe'))
        catalog.validate(windows, **(CONTEXT | dict(target='windows')))

    def test_cli_public_verification_and_prepare(self):
        common = ['--target', 'linux', '--running', '156.0.8073.0', '--installed',
                  '156.0.8073.0', '--now', '2026-10-05T18:00:00Z']
        def run(*args):
            return subprocess.run([sys.executable, str(ROOT/'catalog.py'), *args, *common],
                                  capture_output=True, text=True)
        with tempfile.TemporaryDirectory(prefix='pubky-catalog-test-') as directory:
            root = Path(directory)
            package = root/'package.deb'
            package.write_bytes(b'public unit-test bytes, never an installer')
            output = root/'prepared'
            result = run('prepare', str(FIXTURES/'catalog.json'), '--package', str(package),
                         '--output', str(output))
            self.assertEqual(0, result.returncode, result.stderr)
            r = json.loads((output/'catalog.json').read_bytes())
            self.assertEqual(package.stat().st_size, r['size'])
            self.assertEqual(catalog.DOMAIN + (output/'catalog.json').read_bytes(),
                             (output/'catalog.bin').read_bytes())
            result = run('envelope', str(FIXTURES/'catalog.json'), '--signature',
                         str(FIXTURES/'signature.b64'), '--public-key', str(FIXTURES/'public-key.b64'),
                         '--output', str(root/'envelope.json'))
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(self.envelope, (root/'envelope.json').read_bytes())
            result = run('verify', str(root/'envelope.json'), '--public-key', str(FIXTURES/'public-key.b64'))
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertNotEqual(0, run('prepare', str(FIXTURES/'catalog.json'), '--package',
                                      str(package), '--output', str(output)).returncode)


if __name__ == '__main__':
    unittest.main()
