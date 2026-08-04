#!/usr/bin/env python3
"""Offline unit tests for wasmmod_elf WPSE append/find/strip (no CDN)."""

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def _load(stem: str):
    path = TOOLS / f"{stem}.py"
    spec = importlib.util.spec_from_file_location(stem, path)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _compile_et_rel(tmp: Path) -> bytes:
    src = tmp / "t.c"
    obj = tmp / "t.o"
    src.write_text("int answer(void) { return 42; }\n", encoding="utf-8")
    subprocess.check_call(
        [
            "gcc",
            "-ffreestanding",
            "-fPIC",
            "-fno-plt",
            "-fno-stack-protector",
            "-O2",
            "-c",
            "-o",
            str(obj),
            str(src),
        ]
    )
    return obj.read_bytes()


def test_elf_append_find_strip_roundtrip() -> None:
    elf = _load("wasmmod_elf")
    with tempfile.TemporaryDirectory() as td:
        raw = _compile_et_rel(Path(td))
    assert elf.is_elf64_le(raw)
    payload = b"MPWPtestdata"
    with_sec = elf.append_section(raw, "wasmmod.pack", payload)
    assert with_sec != raw
    assert elf.WPSE_MAGIC in with_sec[-64:]
    found = elf.find_section(with_sec, "wasmmod.pack")
    assert found == payload
    # Dotted and undotted names both match
    assert elf.find_section(with_sec, ".wasmmod.pack") == payload
    stripped = elf.strip_section(with_sec, "wasmmod.pack")
    assert stripped == raw
    assert elf.find_section(stripped, "wasmmod.pack") is None


def test_elf_multi_section_append() -> None:
    elf = _load("wasmmod_elf")
    with tempfile.TemporaryDirectory() as td:
        raw = _compile_et_rel(Path(td))
    a = elf.append_section(raw, "wasmmod.pack", b"PACK")
    b = elf.append_section(a, "wasmmod.imports", b"IMPS")
    assert elf.find_section(b, "wasmmod.pack") == b"PACK"
    assert elf.find_section(b, "wasmmod.imports") == b"IMPS"
    # Strip last append restores prior image (cookie drops prior WPSE on re-append).
    back = elf.strip_section(b, "wasmmod.imports")
    assert elf.find_section(back, "wasmmod.imports") is None
    assert elf.find_section(back, "wasmmod.pack") == b"PACK"


def test_inspect_offline_mpzl_elf_roundtrip(tmp_path: Path) -> None:
    import sys

    if str(TOOLS) not in sys.path:
        sys.path.insert(0, str(TOOLS))
    elf = _load("wasmmod_elf")
    zmod = _load("wasmmod_zlib")
    insp = _load("wasmmod_inspect")
    with tempfile.TemporaryDirectory() as td:
        raw = _compile_et_rel(Path(td))
    packed = elf.append_section(raw, "wasmmod.pack", b"\x00" * 8)
    zlibbed = zmod.wrap_bytes(packed)
    path = tmp_path / "hello.elf.zlib"
    path.write_bytes(zlibbed)
    summary = insp._offline_inspect(path)
    assert summary.get("offline") is True
    # Unwrap must succeed (no bogus MPZL layout error).
    assert "source_error" not in summary or "MPZL" not in str(summary.get("source_error"))
    assert "wasmmod_elf" not in str(summary.get("source_error", ""))
    assert "wasmmod_elf" not in str(summary.get("deps_error", ""))
