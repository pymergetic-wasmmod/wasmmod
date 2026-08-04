#!/usr/bin/env python3

# This file is part of wasmmod, https://github.com/pymergetic/wasmmod
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
Unified wasmmod host tooling.

  python3 tools/wasmmod.py <command> [args…]

Commands:
  pack             Build freestanding Wasm + optional pack section
  pack-tree        Walk nested pack.toml markers → one .wasm per package
  sign             PKI / ECDSA sign .wasm / .aot
  embed-ca         Bake zlib-compressed root CA DER(s) into C
  httpd            Static HTTP server for pack smoke tests
  source           Inspect / extract wasmmod.source section (Python)
  read             Host reader for source/sig (Rust wasmmod-read)
  zlib             Wrap / unwrap whole-artifact MPZL (.wasm.zlib / .aot.zlib)
  publish          One-shot pack → AOT → sign → zlib → metal-cdn upload
  cdn              Remote index / search / show / get (pip-style)
  inspect          Local pack/source/sig summary (+ optional verify)

  read needs: cargo build --release -p wasmmod-read
              (or WASMMOD_READ=/path/to/wasmmod-read)

Each command is also a standalone script: tools/wasmmod_<command>.py
(with hyphens → underscores, e.g. embed-ca → wasmmod_embed_ca.py).
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent

# subcommand → module file stem (without .py)
COMMANDS: dict[str, str] = {
    "pack": "wasmmod_pack",
    "pack-tree": "wasmmod_pack_tree",
    "sign": "wasmmod_sign",
    "embed-ca": "wasmmod_embed_ca",
    "httpd": "wasmmod_httpd",
    "source": "wasmmod_source",
    "read": "wasmmod_read",
    "zlib": "wasmmod_zlib",
    "publish": "wasmmod_publish",
    "cdn": "wasmmod_cdn",
    "inspect": "wasmmod_inspect",
}


def _load(stem: str):
    path = TOOLS_DIR / f"{stem}.py"
    if not path.is_file():
        raise SystemExit(f"wasmmod: missing tool {path}")
    spec = importlib.util.spec_from_file_location(stem, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"wasmmod: cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _usage() -> None:
    print((__doc__ or "").strip(), file=sys.stderr)
    print(file=sys.stderr)
    print("Try: python3 tools/wasmmod.py <command> -h", file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if not args or args[0] in ("-h", "--help"):
        _usage()
        return 0
    cmd = args[0]
    if cmd not in COMMANDS:
        print(f"wasmmod: unknown command {cmd!r}", file=sys.stderr)
        print(f"Known: {', '.join(COMMANDS)}", file=sys.stderr)
        return 2
    stem = COMMANDS[cmd]
    mod = _load(stem)
    if not hasattr(mod, "main"):
        raise SystemExit(f"wasmmod: {stem}.py has no main()")
    # Subtool argparse sees prog as "wasmmod <cmd>" and its own flags only.
    sys.argv = [f"wasmmod {cmd}", *args[1:]]
    cli = _load("wasmmod_cliutil")
    return cli.invoke(mod.main, prog=f"wasmmod {cmd}")


if __name__ == "__main__":
    raise SystemExit(main())
