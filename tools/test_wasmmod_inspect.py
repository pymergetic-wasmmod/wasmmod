"""Offline tests for wasmmod_inspect (needs examples/packs/hello.elf with -g)."""
from __future__ import annotations

from pathlib import Path

import wasmmod_inspect as insp

ELF = Path(__file__).resolve().parents[1] / "examples" / "packs" / "hello.elf"


def test_hello_elf_symbols_and_dwarf() -> None:
    assert ELF.is_file(), f"missing {ELF}; make -C examples/hello_elf"
    data = ELF.read_bytes()
    assert insp.has_dwarf(data)
    names = {s.name for s in insp.list_symbols(data)}
    assert "hello" in names
    assert "add" in names
    assert "hello.c" not in names  # STT_FILE / SHN_ABS filtered
    hello = next(s for s in insp.list_symbols(data) if s.name == "hello")
    assert hello.kind == "func"
    locs = insp.addr2line(data, hello.offset)
    assert locs
    # Without pyelftools: role=sym; with it: dwarf path.
    assert locs[0].role in ("sym", "dwarf")
    assert insp.locations_for_symbol(data, "hello")


def test_locations_source_scan() -> None:
    data = ELF.read_bytes()
    src = {"src/hello.c": "int hello(void) {\n    return 42;\n}\n"}
    locs = insp.locations_for_symbol(data, "hello", source_files=src)
    assert any(l.path == "src/hello.c" and l.line == 1 for l in locs)


def test_disasm_text_nonempty() -> None:
    data = ELF.read_bytes()
    # Find .text index
    text_i = None
    for i, _sh, name in insp._iter_shdrs(data):
        if name == ".text":
            text_i = i
            break
    assert text_i is not None
    hello = next(s for s in insp.list_symbols(data) if s.name == "hello")
    assert hello.section_index == text_i
    lines = insp.disasm(data, text_i, 0, 32)
    assert lines


def test_mpy_disasm_header() -> None:
    fake = b"MP\x06\x00" + bytes(range(12))
    lines = insp.mpy_disasm(fake, limit=8)
    assert lines[0].text.startswith("mpy_hdr")
    assert len(lines) >= 2
