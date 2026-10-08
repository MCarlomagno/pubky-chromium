#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Checks the Debian package name, doc path and copyright rendering.

Uses copies of the real .info, BRANDING and LICENSE files in a temporary
output directory. No package is built.
"""

import os
import pathlib
import shutil
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build

installer = build.installer

LINUX_DIR = pathlib.Path(__file__).resolve().parent.parent
SRC_DIR = LINUX_DIR.parents[2]


class DebPackageTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.out = pathlib.Path(tmp.name)
        for src, dst in [
            (LINUX_DIR / "common/chromium-browser.info", "common/chromium-browser.info"),
            (LINUX_DIR / "common/google-chrome.info", "common/google-chrome.info"),
            (SRC_DIR / "chrome/app/theme/chromium/BRANDING", "theme/BRANDING"),
        ]:
            (self.out / "installer" / dst).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, self.out / "installer" / dst)
        (self.out / "installer/version.txt").write_text(
            "MAJOR=156\nMINOR=0\nBUILD=8073\nPATCH=0\n"
        )

    def config(self, branding, channel):
        args = types.SimpleNamespace(
            branding=branding,
            channel=channel,
            arch="amd64",
            target_os="linux",
            build_time="1791226964",
            official=False,
            use_static_angle="false",
            sysroot="/",
        )
        config = installer.InstallerConfig.from_args(
            args, self.out, package_format=installer.PackageFormat.DEB
        )
        config.script_dir = LINUX_DIR / "debian"
        config.staging_dir = self.out / "staging"
        return config

    def testPackageNameControlChangelogAndDocPath(self):
        for branding, channel, name in [
            ("chromium", "stable", "pubky-chromium"),
            ("chromium", "beta", "pubky-chromium-beta"),
            ("google_chrome", "stable", "google-chrome-stable"),
        ]:
            with self.subTest(branding=branding, channel=channel):
                config = self.config(branding, channel)
                self.assertEqual(name, config.deb_package_name)

                control = self.out / "control"
                installer.process_template(
                    LINUX_DIR / "debian/control.template",
                    control,
                    config.get_template_context(),
                )
                lines = control.read_text().splitlines()
                self.assertIn(f"Source: {name}", lines)
                self.assertIn(f"Package: {name}", lines)

                # debchange only appends a release-notes entry.
                changelog = self.out / "changelog"
                with mock.patch.object(installer, "run_command") as run:
                    installer.gen_changelog(config, changelog)
                self.assertEqual("debchange", run.call_args.args[0][0])
                self.assertTrue(
                    changelog.read_text().startswith(
                        f"{name} (156.0.8073.0-1) {channel};"
                    )
                )
                self.assertTrue(
                    (
                        config.staging_dir / f"usr/share/doc/{name}/changelog.gz"
                    ).is_file()
                )

    def testCopyright(self):
        license_text = (SRC_DIR / "LICENSE").read_text()
        text = build.debian_copyright(
            self.config("chromium", "stable"), license_text
        )
        stripped = "\n".join(
            line[3:] if line.startswith("// ") else line.lstrip("/")
            for line in license_text.splitlines()
        )
        self.assertIn(stripped.strip(), text)
        self.assertIn("Redistribution and use in source and binary forms", text)
        self.assertFalse(
            [line for line in text.splitlines() if line.startswith("//")]
        )
        self.assertIn("/usr/share/doc/pubky-chromium/credits.html", text)
        self.assertIn("BSD-3-Clause", text)

    def testUpdaterOnlyInPubkyStableAmd64Deb(self):
        for branding, channel, expected in [
            ("chromium", "stable", True),
            ("chromium", "beta", False),
            ("google_chrome", "stable", False),
        ]:
            config = self.config(branding, channel)
            artifacts = config.get_binary_artifacts()
            for name, mode in [
                ("pubky-update-coordinator", installer.StandardPermissions.EXECUTABLE),
                ("pubky-update-helper", installer.StandardPermissions.EXECUTABLE),
                ("usr/share/polkit-1/actions/org.pubky.chromium.update.policy",
                 installer.StandardPermissions.REGULAR),
            ]:
                artifact = [a for a in artifacts if a.dst == name]
                self.assertEqual(expected, bool(artifact))
                if artifact:
                    self.assertEqual(mode, artifact[0].mode)
        config = self.config("chromium", "stable")
        config.arch = "arm64"
        self.assertFalse([a for a in config.get_binary_artifacts()
                          if "pubky-update-" in str(a.dst) or "polkit-1" in str(a.dst)])


if __name__ == "__main__":
    unittest.main()
