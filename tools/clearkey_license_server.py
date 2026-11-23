#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Serve a disposable ClearKey license fixture over HTTP; never use production keys."""

import argparse
import base64
import binascii
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

MAX_BODY_SIZE = 1024 * 1024


def decode_id(value):
    if not isinstance(value, str) or not value:
        raise ValueError("KID/key must be a non-empty base64url string")
    try:
        decoded = base64.b64decode(
            value + "=" * (-len(value) % 4), altchars=b"-_", validate=True
        )
    except (ValueError, binascii.Error) as error:
        raise ValueError("Invalid base64url KID/key") from error
    if len(decoded) != 16:
        raise ValueError("ClearKey KID/key must decode to 16 bytes")
    return decoded


def load_keys(path):
    data = path.read_bytes()
    if not data or len(data) > MAX_BODY_SIZE:
        raise ValueError("License fixture must contain 1 byte to 1 MiB")
    license_data = json.loads(data)
    if not isinstance(license_data, dict) or license_data.get("type", "temporary") != "temporary":
        raise ValueError("Only temporary ClearKey licenses are supported")
    keys = license_data.get("keys")
    if not isinstance(keys, list) or not keys:
        raise ValueError("License fixture must contain a non-empty keys list")
    result = {}
    for key in keys:
        if not isinstance(key, dict) or key.get("kty") != "oct":
            raise ValueError("Each ClearKey entry must have kty=oct")
        kid = decode_id(key.get("kid"))
        decode_id(key.get("k"))
        if kid in result:
            raise ValueError("Duplicate KID in license fixture")
        result[kid] = {"kty": "oct", "kid": key["kid"], "k": key["k"]}
    return result


def make_handler(keys):
    class LicenseHandler(BaseHTTPRequestHandler):
        def do_POST(self):
            if self.path != "/license":
                self.send_error(404, "Use /license")
                return
            length = self.headers.get("Content-Length", "")
            if self.headers.get("Transfer-Encoding") or not length.isascii() or not length.isdecimal():
                self.send_error(400, "A decimal Content-Length is required")
                return
            if len(length) > 7 or not 0 < int(length) <= MAX_BODY_SIZE:
                self.send_error(413, "Request must contain 1 byte to 1 MiB")
                return
            self.connection.settimeout(10)
            try:
                body = self.rfile.read(int(length))
            except TimeoutError:
                self.send_error(408, "Timed out reading key request")
                return
            if len(body) != int(length):
                self.send_error(400, "Incomplete key request")
                return
            try:
                request = json.loads(body)
                if not isinstance(request, dict) or request.get("type", "temporary") != "temporary":
                    raise ValueError("Only temporary key requests are supported")
                kids = request.get("kids")
                if not isinstance(kids, list) or not kids:
                    raise ValueError("A non-empty kids list is required")
                requested_ids = [decode_id(kid) for kid in kids]
            except (ValueError, UnicodeDecodeError):
                self.send_error(400, "Invalid ClearKey key request")
                return
            if any(kid not in keys for kid in requested_ids):
                self.send_error(404, "Unknown disposable KID")
                return
            response = json.dumps(
                {"keys": [keys[kid] for kid in requested_ids], "type": "temporary"}
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)

    return LicenseHandler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--license", required=True, type=Path, help="Disposable license.json fixture")
    parser.add_argument("--bind", default="127.0.0.1", help="Bind address; default is loopback only")
    parser.add_argument("--port", type=int, default=8765, help="HTTP port; default is 8765")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    try:
        keys = load_keys(args.license)
    except (OSError, ValueError, UnicodeDecodeError) as error:
        parser.error(f"Cannot load disposable license fixture: {error}")
    with ThreadingHTTPServer((args.bind, args.port), make_handler(keys)) as server:
        print(
            f"Disposable ClearKey server: http://{args.bind}:{server.server_port}/license",
            flush=True,
        )
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
