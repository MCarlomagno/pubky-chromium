#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Prepare and publicly verify v1 catalog bytes. This tool never signs."""

import argparse
import base64
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

DOMAIN = b"PubkyChromiumUpdateCatalogV1\n"
MAX_ENVELOPE = 128 * 1024
MAX_PAYLOAD = 64 * 1024
STRINGS = {
    "architecture", "asset", "channel", "expires_at", "issued_at",
    "native_version", "package_id", "platform", "product", "product_version",
    "repository", "sha256", "source_revision", "tag", "upstream_version", "url",
}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True).encode("ascii")


def pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate field")
        result[key] = value
    return result


def parse(data, limit):
    if len(data) > limit:
        raise ValueError("input exceeds bound")
    return json.loads(data, object_pairs_hook=pairs)


def read(path, limit):
    with Path(path).open("rb") as file:
        data = file.read(limit + 1)
    if len(data) > limit:
        raise ValueError("input exceeds bound")
    return data


def version(text):
    if not isinstance(text, str) or not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", text):
        raise ValueError("noncanonical four-part version")
    parts = tuple(map(int, text.split(".")))
    if any(n > 65535 for n in parts):
        raise ValueError("version exceeds PE component limit")
    return parts


def timestamp(text):
    if not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", text):
        raise ValueError("timestamp must use UTC seconds")
    return dt.datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=dt.timezone.utc)


def validate(record, target, running, installed, floor, now):
    if not isinstance(record, dict) or set(record) != STRINGS | {"schema", "size"}:
        raise ValueError("closed schema required")
    for field in STRINGS:
        s = record[field]
        if not isinstance(s, str) or not 0 < len(s) <= 2048 or any(
                not 0x20 <= ord(c) <= 0x7e or c in '\\"<>' for c in s):
            raise ValueError("restricted printable ASCII required")
    if type(record["schema"]) is not int or record["schema"] != 1:
        raise ValueError("wrong schema")
    if type(record["size"]) is not int or not 0 < record["size"] <= 2147483647:
        raise ValueError("invalid package size")
    if target not in ("linux", "windows"):
        raise ValueError("unsupported target")
    expected = {"repository": "MCarlomagno/pubky-chromium", "product": "Pubky Chromium",
                "channel": "experimental", "architecture": "x64", "platform": target,
                "package_id": "pubky-chromium" if target == "linux" else "PubkyChromium"}
    if any(record[k] != v for k, v in expected.items()):
        raise ValueError("target or identity mismatch")
    for field in ("tag", "asset"):
        if not re.fullmatch(r"[A-Za-z0-9._-]{1,200}", record[field]) or record[field] in (".", ".."):
            raise ValueError("invalid release path segment")
    if not record["asset"].endswith(".deb" if target == "linux" else ".exe"):
        raise ValueError("wrong package format")
    expected_url = ("https://github.com/MCarlomagno/pubky-chromium/releases/download/"
                    + record["tag"] + "/" + record["asset"])
    if record["url"] != expected_url:
        raise ValueError("package origin/path mismatch")
    for field, length in (("sha256", 64), ("source_revision", 40)):
        if not re.fullmatch(r"[0-9a-f]{" + str(length) + "}", record[field]):
            raise ValueError("invalid digest or source revision")
    product = version(record["product_version"])
    version(record["upstream_version"])
    if record["native_version"] != record["product_version"] + ("-1" if target == "linux" else ""):
        raise ValueError("native version must match generated product tuple")
    if product <= version(running) or product <= version(installed):
        raise ValueError("not newer than running and installed versions")
    if floor and product < version(floor):
        raise ValueError("replayed offer below floor")
    issued, expires = timestamp(record["issued_at"]), timestamp(record["expires_at"])
    if not dt.timedelta(0) < expires - issued <= dt.timedelta(days=180):
        raise ValueError("invalid metadata lifetime")
    if issued > now + dt.timedelta(minutes=5) or now >= expires:
        raise ValueError("expired metadata or clock error")


def decode(text, size=None):
    if not isinstance(text, str):
        raise ValueError("base64 string required")
    data = base64.b64decode(text, validate=True)
    if base64.b64encode(data).decode() != text or (size is not None and len(data) != size):
        raise ValueError("noncanonical base64 or wrong length")
    return data


def verify_signature(payload, signature, public_key):
    # RFC 8410 SubjectPublicKeyInfo prefix for a raw Ed25519 public key.
    der = bytes.fromhex("302a300506032b6570032100") + public_key
    with tempfile.TemporaryDirectory(prefix="pubky-public-verify-") as directory:
        root = Path(directory)
        (root / "public.der").write_bytes(der)
        (root / "message.bin").write_bytes(DOMAIN + payload)
        (root / "signature.bin").write_bytes(signature)
        result = subprocess.run([
            "openssl", "pkeyutl", "-verify", "-pubin", "-keyform", "DER",
            "-inkey", str(root / "public.der"), "-rawin", "-in", str(root / "message.bin"),
            "-sigfile", str(root / "signature.bin")], capture_output=True, check=False)
    if result.returncode:
        raise ValueError("Ed25519 signature verification failed")


def verify_envelope(data, key, **context):
    outer = parse(data, MAX_ENVELOPE)
    if not isinstance(outer, dict) or set(outer) != {"format", "payload", "signature"} or outer["format"] != "pubky-updates-v1" or canonical(outer) != data:
        raise ValueError("invalid canonical envelope")
    payload = decode(outer["payload"])
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds bound")
    signature = decode(outer["signature"], 64)
    verify_signature(payload, signature, key)
    record = parse(payload, MAX_PAYLOAD)
    if canonical(record) != payload:
        raise ValueError("noncanonical payload")
    validate(record, **context)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "envelope", "verify"))
    parser.add_argument("input", type=Path)
    parser.add_argument("--target", choices=("linux", "windows"), required=True)
    parser.add_argument("--running", required=True)
    parser.add_argument("--installed", required=True)
    parser.add_argument("--floor", default="")
    parser.add_argument("--now", help="UTC timestamp; defaults to current UTC")
    parser.add_argument("--package", type=Path, help="prepare: frozen package bytes to hash")
    parser.add_argument("--signature", type=Path)
    parser.add_argument("--public-key", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    context = dict(target=args.target, running=args.running, installed=args.installed,
                   floor=args.floor, now=timestamp(args.now) if args.now else dt.datetime.now(dt.timezone.utc))
    data = read(args.input, MAX_ENVELOPE)
    if args.action == "prepare":
        if not args.package or not args.output:
            parser.error("prepare requires --package and a new --output directory")
        record = parse(data, MAX_PAYLOAD)
        digest = hashlib.sha256()
        size = 0
        with args.package.open("rb") as package:
            for chunk in iter(lambda: package.read(1024 * 1024), b""):
                size += len(chunk)
                if size > 2147483647:
                    raise ValueError("package exceeds bound")
                digest.update(chunk)
        record["size"], record["sha256"] = size, digest.hexdigest()
        validate(record, **context)
        payload = canonical(record)
        if len(payload) > MAX_PAYLOAD:
            raise ValueError("payload exceeds bound")
        args.output.mkdir(exist_ok=False)
        (args.output / "catalog.json").write_bytes(payload)
        (args.output / "catalog.bin").write_bytes(DOMAIN + payload)
    else:
        if not args.public_key:
            parser.error("public verification requires --public-key")
        key = decode(read(args.public_key, 128).decode("ascii").strip(), 32)
        if args.action == "envelope":
            if not args.signature or not args.output:
                parser.error("envelope requires --signature and --output")
            signature = decode(read(args.signature, 256).decode("ascii").strip(), 64)
            outer = dict(format="pubky-updates-v1", payload=base64.b64encode(data).decode(),
                         signature=base64.b64encode(signature).decode())
            data = canonical(outer)
            verify_envelope(data, key, **context)
            with args.output.open("xb") as output:
                output.write(data)
        else:
            verify_envelope(data, key, **context)
    print("Public catalog inputs validated; no signing or installation performed.")


if __name__ == "__main__":
    main()
