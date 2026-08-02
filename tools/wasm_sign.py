#!/usr/bin/env python3

# This file is part of the MicroPython project, http://micropython.org/
#
# The MIT License (MIT)
#
# Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
"""
Detach-sign a .wasm/.aot with ECDSA-P256+SHA256 for MICROPY_WASM_VERIFY.

Uses openssl if available (from this repo root):
  tools/wasm_sign.py gen-key -o testkey
  tools/wasm_sign.py sign --key testkey.pem hello.wasm
  # emits hello.wasm.sig; public key: testkey.pub.der for wasm.add_trust()
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), file=sys.stderr)
    subprocess.check_call(cmd)


def cmd_gen_key(out_prefix: Path) -> None:
    pem = out_prefix.with_suffix(".pem")
    pub = Path(str(out_prefix) + ".pub.der")
    run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(pem)])
    run(["openssl", "ec", "-in", str(pem), "-pubout", "-outform", "DER", "-out", str(pub)])
    print(pem)
    print(pub)


def cmd_sign(key: Path, target: Path) -> None:
    sig = Path(str(target) + ".sig")
    run(["openssl", "dgst", "-sha256", "-sign", str(key), "-out", str(sig), str(target)])
    print(sig)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen-key")
    g.add_argument("-o", "--output", type=Path, required=True, help="key prefix")
    s = sub.add_parser("sign")
    s.add_argument("--key", type=Path, required=True, help="private key PEM")
    s.add_argument("target", type=Path, help=".wasm / .aot path")
    args = ap.parse_args()
    if args.cmd == "gen-key":
        cmd_gen_key(args.output)
    else:
        cmd_sign(args.key, args.target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
