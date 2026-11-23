#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate disposable ClearKey fixtures for the DRM L2/L3 VTS.

Writes a random KID/key/IV, a PSSH init-data blob, a W3C ClearKey license
response, a known plaintext and its AES-128-CTR ciphertext (via the openssl
command line tool), plus env.sh exporting the DRM_L2_* and DRM_L3_* variables.

    tools/make_clearkey_fixtures.py /tmp/clearkey_fixtures
    . /tmp/clearkey_fixtures/env.sh
"""
import argparse
import base64
import os
import struct
import subprocess
import sys

CLEARKEY_UUID = bytes.fromhex("e2719d58a985b3c9781ab030af78d30e")


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def pssh_v1(system_id: bytes, kids) -> bytes:
    body = struct.pack(">I", len(kids)) + b"".join(kids) + struct.pack(">I", 0)
    size = 4 + 4 + 4 + len(system_id) + len(body)
    return struct.pack(">I", size) + b"pssh" + struct.pack(">I", 1 << 24) + system_id + body


def aes_ctr(key: bytes, iv: bytes, data: bytes) -> bytes:
    return subprocess.run(
        ["openssl", "enc", "-aes-128-ctr", "-K", key.hex(), "-iv", iv.hex()],
        input=data, capture_output=True, check=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("outdir")
    parser.add_argument("--size", type=int, default=4096, help="sample size in bytes")
    parser.add_argument("--mime", default="video/mp4")
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    kid, key, iv = os.urandom(16), os.urandom(16), os.urandom(16)
    plain = os.urandom(args.size)
    cipher = aes_ctr(key, iv, plain)
    license_json = ('{"keys":[{"kty":"oct","kid":"%s","k":"%s"}],"type":"temporary"}'
                    % (b64url(kid), b64url(key)))

    files = {
        "init_data.bin": pssh_v1(CLEARKEY_UUID, [kid]),
        "license.json": license_json.encode(),
        "plain.bin": plain,
        "cipher.bin": cipher,
    }
    for name, content in files.items():
        with open(os.path.join(args.outdir, name), "wb") as handle:
            handle.write(content)

    out = os.path.abspath(args.outdir)
    env = {
        "DRM_FACTORY_INSTANCE": "clearkey",
        "DRM_L2_ENABLE": "1",
        "DRM_L2_INIT_DATA": f"{out}/init_data.bin",
        "DRM_L2_CIPHERTEXT": f"{out}/cipher.bin",
        "DRM_L2_PLAINTEXT": f"{out}/plain.bin",
        "DRM_L2_KID_HEX": kid.hex(),
        "DRM_L2_KEY_HEX": key.hex(),
        "DRM_L2_IV_HEX": iv.hex(),
        "DRM_L2_LICENSE_RESPONSE": f"{out}/license.json",
        "DRM_L3_ENABLE": "1",
        "DRM_L3_INIT_DATA": f"{out}/init_data.bin",
        "DRM_L3_ENCRYPTED_SAMPLE": f"{out}/cipher.bin",
        "DRM_L3_EXPECTED_PLAINTEXT": f"{out}/plain.bin",
        "DRM_L3_LICENSE_RESPONSE": f"{out}/license.json",
        "DRM_L3_KID_HEX": kid.hex(),
        "DRM_L3_IV_HEX": iv.hex(),
        "DRM_L3_MIME_TYPE": args.mime,
        "DRM_L3_MODE": "AES_CTR",
    }
    with open(os.path.join(args.outdir, "env.sh"), "w") as handle:
        for name, value in env.items():
            handle.write(f"export {name}={value}\n")
    print(f"Wrote fixtures and env.sh to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
