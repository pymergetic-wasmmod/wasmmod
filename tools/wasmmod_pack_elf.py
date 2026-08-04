#!/usr/bin/env python3
"""Embed wasmmod.pack (+ imports/deps) into an ELF64 ET_REL object (→ .elf pack)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import wasmmod_elf as elf  # noqa: E402
import wasmmod_pack as pack  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--obj", required=True, type=Path, help="input ET_REL .o/.elf")
    ap.add_argument("--manifest", required=True, type=Path, help="pack.toml")
    ap.add_argument("-o", "--output", required=True, type=Path)
    args = ap.parse_args(argv)

    data = pack.load_toml(args.manifest)
    name = data.get("name") or args.output.stem.split(".")[0]
    exports: list[tuple[str, str, str, int]] = []
    for item in data.get("exports") or []:
        if not isinstance(item, dict):
            continue
        func = item.get("func") or ""
        export_name = item.get("export") or func
        module = item.get("module") or ""
        sig_s = item.get("sig") if isinstance(item.get("sig"), str) else None
        exports.append((module, func, export_name, pack.sig_tag(sig_s)))

    imports: list[tuple[str, str]] = []
    for item in data.get("imports") or []:
        if not isinstance(item, dict):
            continue
        mod = item.get("module") or ""
        func = item.get("func") or ""
        if mod and func:
            imports.append((mod, func))

    deps = pack.parse_manifest_deps(data)

    compress = bool((data.get("pack") or {}).get("compress", False))
    payload = pack.build_pack_payload(name, [], exports, compress=compress)
    raw = args.obj.read_bytes()
    out = elf.append_section(raw, "wasmmod.pack", payload)
    if imports:
        out = elf.append_section(out, pack.IMPORTS_SECTION, pack.build_imports_payload(imports))
    if deps:
        out = elf.append_section(out, pack.DEPS_SECTION, pack.build_deps_payload(deps))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(out)
    print(
        f"wrote {args.output} pack={name} exports={len(exports)} "
        f"imports={len(imports)} deps={len(deps)}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
