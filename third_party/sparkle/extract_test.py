# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import tempfile
import unittest

from extract import extract


class ExtractTest(unittest.TestCase):
    def test_pinned_archive_and_symlinks(self):
        archive = pathlib.Path(__file__).with_name("Sparkle-2.10.0.tar.xz")
        with tempfile.TemporaryDirectory() as folder:
            destination = pathlib.Path(folder)
            extract(archive, destination, destination / "extract.stamp")
            framework = destination / "Sparkle.framework"
            self.assertTrue((framework / "Versions" / "Current").is_symlink())
            self.assertTrue((framework / "Sparkle").is_file())
            self.assertTrue((destination / "extract.stamp").is_file())
            altered = destination / "bad.tar.xz"
            altered.write_bytes(archive.read_bytes()[:100])
            with self.assertRaisesRegex(ValueError, "digest mismatch"):
                extract(altered, destination, destination / "bad.stamp")


if __name__ == "__main__":
    unittest.main()
