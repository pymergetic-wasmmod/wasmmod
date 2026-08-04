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
Unified local artifact inspect (pack + source + sig).

  tools/wasmmod.py inspect PATH.wasm [--json] [--verify --trust root.crt]

With ``pymergetic-metal-cdn-client``: rich typed dump via ``inspect_artifact``.
Without it: offline fallback via ``source list`` + ``sign info``.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parent
PROG = "wasmmod inspect"


def _load(stem: str):
    path = TOOLS_DIR / f"{stem}.py"
    spec = importlib.util.spec_from_file_location(stem, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"{PROG}: cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _cli():
    return _load("wasmmod_cliutil")


def _offline_inspect(path: Path) -> dict[str, Any]:
    """Compose a basic summary without the PyPI client."""
    out: dict[str, Any] = {"path": str(path), "offline": True}
    source = _load("wasmmod_source")
    data = path.read_bytes()
    try:
        # MPZL unwrap if present (client-free): magic(4) + u32le raw_len + zlib body
        if data[:4] == b"MPZL":
            zmod = _load("wasmmod_zlib")
            data = zmod.unwrap_bytes(data)
        payload = source.extract_custom_section(data, "wasmmod.source")
        if payload:
            meta = source.parse_source_payload(payload)
            out["source_name"] = meta.get("name")
            out["source_version"] = meta.get("pkg_version")
            out["source_files"] = [
                e.get("path") for e in (meta.get("files") or []) if isinstance(e, dict)
            ]
        else:
            out["source_files"] = []
    except Exception as exc:  # noqa: BLE001 — offline best-effort
        out["source_error"] = str(exc)

    try:
        pack = _load("wasmmod_pack")
        deps_sec = None
        if hasattr(pack, "extract_custom_section"):
            deps_sec = pack.extract_custom_section(data, pack.DEPS_SECTION)
        else:
            # Fall back via source helper which also extracts customs
            deps_sec = source.extract_custom_section(data, "wasmmod.deps")
        if deps_sec:
            out["deps"] = [
                {"name": n, "version": v} for n, v in pack.parse_deps_payload(deps_sec)
            ]
        else:
            out["deps"] = []
    except Exception as exc:  # noqa: BLE001
        out["deps_error"] = str(exc)

    sign_py = TOOLS_DIR / "wasmmod_sign.py"
    try:
        proc = subprocess.run(
            [sys.executable, str(sign_py), "info", str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        out["sign_info"] = (proc.stdout or proc.stderr or "").strip()
        if proc.returncode != 0 and not out["sign_info"]:
            out["sign_error"] = f"exit {proc.returncode}"
    except OSError as exc:
        out["sign_error"] = str(exc)
    return out


def _print_rich(contents: Any) -> None:
    dump = contents.model_dump() if hasattr(contents, "model_dump") else contents
    if not isinstance(dump, dict):
        print(dump)
        return
    print(f"kind={dump.get('kind')} encoding={dump.get('encoding')} signed={dump.get('signed')}")
    if dump.get("error"):
        print(f"error: {dump['error']}")
    pack = dump.get("pack")
    if isinstance(pack, dict):
        print(f"pack: {pack.get('name')} v{pack.get('version')}")
        for f in pack.get("files") or []:
            if isinstance(f, dict):
                print(f"  pack/{f.get('path')}  {f.get('kind')}  {f.get('raw_len')}B")
    source = dump.get("source")
    if isinstance(source, dict):
        print(f"source: {source.get('name')} {source.get('pkg_version') or ''}".rstrip())
        for f in source.get("files") or []:
            if isinstance(f, dict):
                print(f"  src/{f.get('path')}  {f.get('raw_len')}B")
    sig = dump.get("sig")
    if isinstance(sig, dict):
        print(
            f"sig: format={sig.get('format')} flags={sig.get('flags')} "
            f"sig_len={sig.get('sig_len')} chain_len={sig.get('chain_len')}"
        )
    elif dump.get("sig_error"):
        print(f"sig error: {dump['sig_error']}")
    deps = dump.get("deps")
    if isinstance(deps, list) and deps:
        print("deps:")
        for d in deps:
            if isinstance(d, dict):
                print(f"  {d.get('name')}@{d.get('version')}")
            else:
                print(f"  {d}")
    elif dump.get("deps_error"):
        print(f"deps error: {dump['deps_error']}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", type=Path, help="Local .wasm / .aot / .elf / .zlib")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--verify", action="store_true", help="Verify MPWS against --trust roots")
    ap.add_argument(
        "--trust",
        type=Path,
        action="append",
        default=[],
        help="Root CA PEM/DER (repeatable); used with --verify",
    )
    args = ap.parse_args(argv)
    cli = _cli()
    path: Path = args.path
    if not path.is_file():
        cli.die(PROG, f"not a file: {path}")

    data = path.read_bytes()
    rich = None
    try:
        from pymergetic.metal.cdn_client.contents import inspect_artifact

        rich = inspect_artifact(data, filename=path.name)
    except ImportError:
        rich = None

    if args.verify:
        roots = [p.read_bytes() for p in args.trust]
        if not roots:
            cli.die(PROG, "--verify requires at least one --trust root")
        try:
            from pymergetic.metal.cdn_client.verify import verify_artifact

            result = verify_artifact(data, trust_roots=roots, filename=path.name)
            if args.json:
                print(
                    json.dumps(
                        {
                            "ok": result.ok,
                            "error": result.error,
                            "signed": result.signed,
                            "format": result.format,
                            "leaf_sha256": result.leaf_sha256,
                        },
                        indent=2,
                    )
                )
            else:
                if result.ok:
                    print(f"verify: ok ({result.format})")
                else:
                    print(f"verify: FAIL — {result.error}", file=sys.stderr)
                    return 1
            if rich is None:
                return 0 if result.ok else 1
        except ImportError:
            # openssl path
            sign_py = TOOLS_DIR / "wasmmod_sign.py"
            cmd = [sys.executable, str(sign_py), "verify", str(path)]
            for root in args.trust:
                cmd.extend(["--trust", str(root)])
            proc = subprocess.run(cmd, check=False)
            if proc.returncode != 0:
                return proc.returncode

    if rich is not None:
        if args.json:
            dump = rich.model_dump() if hasattr(rich, "model_dump") else rich
            print(json.dumps(dump, indent=2, default=str))
        else:
            _print_rich(rich)
        return 0

    # Offline fallback
    summary = _offline_inspect(path)
    if args.json:
        print(json.dumps(summary, indent=2, default=str))
        return 0
    print(f"path: {path} (offline inspect — install client for full dump)")
    if summary.get("source_files"):
        print("source files:")
        for p in summary["source_files"]:
            print(f"  {p}")
    elif summary.get("source_note"):
        print(summary["source_note"])
    if summary.get("source_error"):
        print(f"source: {summary['source_error']}")
    deps = summary.get("deps")
    if isinstance(deps, list) and deps:
        print("deps:")
        for d in deps:
            if isinstance(d, dict):
                print(f"  {d.get('name')}@{d.get('version')}")
    elif summary.get("deps_error"):
        print(f"deps: {summary['deps_error']}")
    if summary.get("sign_info"):
        print(summary["sign_info"])
    if summary.get("sign_error"):
        print(f"sign: {summary['sign_error']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli().invoke(main, prog=PROG))
