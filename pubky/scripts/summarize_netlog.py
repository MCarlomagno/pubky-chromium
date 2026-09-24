#!/usr/bin/env python3
"""Verify and summarize the two acceptance-test connections in a closed NetLog."""
import argparse
import base64
import json
from pathlib import Path

from probe_endpoint import DOH, decode_key

HOSTS = {
    "4msqbgpkfcdgnzrrsyp5hgno8rfa4sx15c79ughsq95ikycunowy": 443,
    "8um71us3fyw6h8wbcxb5ar3rwusy1a6u49956ikzojg3gcwd1dty": 6287,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("netlog", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    events = json.loads(args.netlog.read_text())["events"]
    configs = [e["params"] for e in events if "doh_config" in e.get("params", {})]
    if not any(c.get("secure_dns_mode") == 2 and
               c["doh_config"].get("servers") == [{"template": DOH}]
               for c in configs):
        raise SystemExit("No strict PKDNS configuration found")
    if any("homeserver.pubky.app" in e.get("params", {}).get("url", "")
           for e in events):
        raise SystemExit("Unexpected ICANN homeserver URL in capture")

    summary = {"doh": DOH, "dns_mode": "secure", "connections": []}
    for host, port in HOSTS.items():
        url = f"https://{host}/"
        if not any(e.get("params", {}).get("url") == url and
                   e["params"].get("request_type") == "main frame"
                   for e in events):
            raise SystemExit(f"No main-frame request for {host}")
        if not any(e.get("params", {}).get("host") == f"https://{host}" and
                   e["params"].get("secure_dns_mode") == 2 for e in events):
            raise SystemExit(f"No secure DNS lookup for {host}")

        expected = base64.b64encode(decode_key(host)).decode()
        tls_events = [e for e in events
                      if e.get("params", {}).get("peer_authentication") == "RawPublicKey"
                      and e["params"].get("verified_raw_public_key") == expected
                      and e["params"].get("version") == "TLS 1.3"
                      and e["params"].get("peer_signature_algorithm") == 0x0807]
        if not tls_events:
            raise SystemExit(f"No matching authenticated raw-key TLS connection for {host}")
        socket_ids = {e["source"]["id"] for e in tls_events}
        peers = sorted({e["params"]["remote_address"] for e in events
                        if e["source"]["id"] in socket_ids and
                        "remote_address" in e.get("params", {})})
        if not peers or not all(peer.endswith(f":{port}") for peer in peers):
            raise SystemExit(f"Unexpected transport endpoints for {host}: {peers}")
        summary["connections"].append({
            "url": url, "endpoints": peers, "tls": "TLS 1.3",
            "authentication": "RFC 7250 RawPublicKey",
            "signature": "Ed25519", "url_key_matches_peer": True,
            "public_key_hex": decode_key(host).hex(),
        })
    text = json.dumps(summary, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
