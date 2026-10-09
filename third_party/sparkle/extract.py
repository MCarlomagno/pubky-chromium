# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unpack the pinned Sparkle binary without flattening framework symlinks."""

import hashlib
import pathlib
import subprocess
import sys

DIGEST = "c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c"


def extract(archive, destination, stamp):
    archive, destination, stamp = map(pathlib.Path, (archive, destination, stamp))
    if hashlib.sha256(archive.read_bytes()).hexdigest() != DIGEST:
        raise ValueError("Sparkle release digest mismatch")
    destination.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["tar", "-xJf", str(archive), "-C", str(destination), "./Sparkle.framework"],
        check=True,
    )
    stamp.write_text(DIGEST + "\n", encoding="ascii")


if __name__ == "__main__":
    extract(*sys.argv[1:])
