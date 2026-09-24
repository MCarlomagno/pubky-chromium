#!/usr/bin/env python3
"""Resolve through PKDNS and verify RFC 7250 TLS against the URL's key.

Requires OpenSSL 3.2+ with -enable_server_rpk. No PKARR relay or system
resolver is used for the destination. System DNS may bootstrap the DoH host.
"""

import argparse
import ipaddress
import json
import struct
import subprocess
import urllib.request

ALPHABET = "ybndrfg8ejkmcpqxot1uwisza345h769"
DOH = "https://pkdns.pubky.org/dns-query"


def decode_key(host):
    if len(host) != 52 or any(c not in ALPHABET for c in host):
        raise ValueError("Expected a canonical 52-character public key")
    bits = "".join(f"{ALPHABET.index(c):05b}" for c in host)
    if bits[256:] != "0000":
        raise ValueError("Noncanonical public-key padding")
    return int(bits[:256], 2).to_bytes(32, "big")


def name_at(data, offset, seen=None):
    seen = set() if seen is None else set(seen)
    labels = []
    while True:
        if offset in seen:
            raise ValueError("DNS compression loop")
        seen.add(offset)
        length = data[offset]
        offset += 1
        if length == 0:
            return ".".join(labels) or ".", offset
        if length & 0xC0 == 0xC0:
            pointer = ((length & 0x3F) << 8) | data[offset]
            suffix, _ = name_at(data, pointer, seen)
            labels.append(suffix)
            return ".".join(labels), offset + 1
        if length > 63 or offset + length > len(data):
            raise ValueError("Invalid DNS label")
        labels.append(data[offset:offset + length].decode("ascii"))
        offset += length


def query(host, qtype):
    message = (struct.pack("!6H", 0, 256, 1, 0, 0, 0)
               + bytes([len(host)]) + host.encode() + b"\0"
               + struct.pack("!2H", qtype, 1))
    request = urllib.request.Request(
        DOH, data=message,
        headers={"Accept": "application/dns-message",
                 "Content-Type": "application/dns-message"})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
    ident, flags, questions, answers, _, _ = struct.unpack("!6H", data[:12])
    if ident != 0 or not flags & 0x8000 or flags & 0x0200 or flags & 15:
        raise ValueError(f"DNS query {qtype} failed: flags={flags:#06x}")
    offset = 12
    for _ in range(questions):
        _, offset = name_at(data, offset)
        offset += 4
    records = []
    for _ in range(answers):
        owner, offset = name_at(data, offset)
        typ, cls, ttl, size = struct.unpack_from("!HHIH", data, offset)
        offset += 10
        end = offset + size
        if end > len(data):
            raise ValueError("Truncated DNS answer")
        record = {"name": owner, "type": typ, "ttl": ttl}
        if cls == 1 and typ in (1, 28):
            record["address"] = str(ipaddress.ip_address(data[offset:end]))
        elif cls == 1 and typ == 65:
            record["priority"] = struct.unpack_from("!H", data, offset)[0]
            record["target"], pos = name_at(data, offset + 2)
            record["params"] = {}
            while pos < end:
                key, length = struct.unpack_from("!HH", data, pos)
                pos += 4
                value = data[pos:pos + length]
                pos += length
                if pos > end:
                    raise ValueError("Truncated HTTPS parameter")
                record["params"][key] = value.hex()
                if key == 3:
                    record["port"] = struct.unpack("!H", value)[0]
        records.append(record)
        offset = end
    return records


def handshake(host, address, port, key, wrong=False):
    if wrong:
        key = bytes([key[0] ^ 1]) + key[1:]
    # Pin the SPKI directly through OpenSSL's DANE verifier. This TLSA value
    # is derived locally from the URL; it is not obtained from DNS.
    spki = bytes.fromhex("302a300506032b6570032100") + key
    command = ["openssl", "s_client", "-connect", f"{address}:{port}",
               "-servername", host, "-enable_server_rpk", "-tls1_3",
               "-dane_tlsa_domain", host, "-dane_tlsa_rrdata",
               "3 1 0 " + spki.hex(), "-verify_return_error", "-brief",
               "-ign_eof"]
    request = f"GET / HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n"
    result = subprocess.run(command, input=request, capture_output=True,
                            text=True, timeout=30)
    if wrong:
        if result.returncode == 0 or "no matching DANE TLSA records" not in result.stderr:
            raise RuntimeError("Wrong-key rejection not established: " + result.stderr)
        return {"wrong_key_rejected": True}
    if result.returncode != 0:
        raise RuntimeError(result.stderr)
    if "Peer used raw public key" not in result.stderr:
        raise RuntimeError("RFC 7250 was not negotiated: " + result.stderr)
    if "HTTP/1.1 200" not in result.stdout:
        raise RuntimeError("Endpoint did not return HTTP 200: " + result.stdout)
    return {"tls_diagnostics": result.stderr.strip(),
            "http_response": result.stdout}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", default=
                        "4msqbgpkfcdgnzrrsyp5hgno8rfa4sx15c79ughsq95ikycunowy")
    parser.add_argument("--port", type=int,
                        help="Explicit transport port for diagnostics; bypass HTTPS-record selection")
    args = parser.parse_args()
    if args.port is not None and not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    key = decode_key(args.host)
    records = {str(t): query(args.host, t) for t in (1, 28, 65)}
    print(json.dumps({"doh": DOH, "records": records}, indent=2), flush=True)
    addresses = [r["address"] for r in records["1"]
                 if r["name"] == args.host and "address" in r]
    endpoints = [r for r in records["65"] if r.get("priority", 0) > 0
                 and r.get("target") == "." and r["name"] == args.host]
    if not addresses:
        raise RuntimeError("No A record for the requested public-key host")
    if args.port is not None:
        port = args.port
        print(json.dumps({"endpoint_selection": "explicit_port", "port": port}))
    elif not endpoints:
        raise RuntimeError("Need an A record and a ServiceMode HTTPS record with target=.")
    else:
        endpoint = min(endpoints, key=lambda r: r["priority"])
        port = endpoint.get("port", 443)
    print(json.dumps(handshake(args.host, addresses[0], port, key), indent=2))
    print(json.dumps(handshake(args.host, addresses[0], port, key, wrong=True)))


if __name__ == "__main__":
    main()
