#!/usr/bin/env python3
"""Host-side pack inspect: symbols, addr2line/locations, disasm, mpy-dis.

Shared by CDN client, CLI, and (later) thin C/Rust peers. No MicroPython.
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field
from io import BytesIO
from pathlib import Path
from typing import Iterable, Optional

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import wasmmod_elf as elf  # noqa: E402

SHT_SYMTAB = 2
SHT_STRTAB = 3
STT_FUNC = 2
STT_OBJECT = 1
STB_LOCAL = 0
STB_GLOBAL = 1
STB_WEAK = 2
EM_X86_64 = 62
EM_AARCH64 = 183
SHN_UNDEF = 0
SHN_LORESERVE = 0xFF00
SHN_ABS = 0xFFF1
SHN_COMMON = 0xFFF2
STT_FILE = 4

Elf64_Sym = struct.Struct("<IBBHQQ")  # name, info, other, shndx, value, size


@dataclass
class Location:
    path: str
    line: int | None = None
    role: str = "dwarf"  # def|decl|include|twin|dwarf|sym


@dataclass
class Symbol:
    name: str
    section_index: int | None
    offset: int
    size: int
    kind: str  # func|data|export|other
    binding: str = ""


@dataclass
class DisasmLine:
    addr: int
    raw: bytes
    text: str


def _iter_shdrs(buf: bytes) -> Iterable[tuple[int, dict, Optional[str]]]:
    if not elf.is_elf64_le(buf):
        return
        yield  # pragma: no cover — make this a generator
    eh = elf._parse_ehdr(buf)
    shstr = elf._shdr(buf, eh, eh["e_shstrndx"])
    for i in range(eh["e_shnum"]):
        sh = elf._shdr(buf, eh, i)
        # extend _shdr keys used below
        off = eh["e_shoff"] + i * eh["e_shentsize"]
        (
            sh_name,
            sh_type,
            sh_flags,
            sh_addr,
            sh_offset,
            sh_size,
            sh_link,
            sh_info,
            sh_addralign,
            sh_entsize,
        ) = elf.Shdr.unpack_from(buf, off)
        sh = {
            "sh_name": sh_name,
            "sh_type": sh_type,
            "sh_flags": sh_flags,
            "sh_addr": sh_addr,
            "sh_offset": sh_offset,
            "sh_size": sh_size,
            "sh_link": sh_link,
            "sh_info": sh_info,
            "sh_addralign": sh_addralign,
            "sh_entsize": sh_entsize,
        }
        yield i, sh, elf._sh_name(buf, eh, shstr, sh["sh_name"])


def _eh_machine(buf: bytes) -> int:
    return struct.unpack_from("<H", buf, 18)[0]


def has_dwarf(buf: bytes) -> bool:
    if not elf.is_elf64_le(buf):
        return False
    for _i, _sh, name in _iter_shdrs(buf):
        if name in (".debug_line", ".debug_info"):
            return True
    return False


def list_symbols(buf: bytes) -> list[Symbol]:
    if elf.is_elf64_le(buf):
        return _list_symbols_elf(buf)
    if len(buf) >= 4 and buf[:4] == b"\x00asm":
        return _list_symbols_wasm(buf)
    return []


def _bind_name(info: int) -> str:
    b = info >> 4
    return {STB_LOCAL: "local", STB_GLOBAL: "global", STB_WEAK: "weak"}.get(b, str(b))


def _list_symbols_elf(buf: bytes) -> list[Symbol]:
    out: list[Symbol] = []
    for _i, sh, name in _iter_shdrs(buf):
        if sh["sh_type"] != SHT_SYMTAB or sh["sh_entsize"] < Elf64_Sym.size:
            continue
        link = sh["sh_link"]
        str_sh = None
        for j, sj, _nj in _iter_shdrs(buf):
            if j == link:
                str_sh = sj
                break
        if str_sh is None or str_sh["sh_type"] != SHT_STRTAB:
            continue
        strtab = buf[str_sh["sh_offset"] : str_sh["sh_offset"] + str_sh["sh_size"]]
        n = sh["sh_size"] // sh["sh_entsize"]
        for k in range(n):
            off = sh["sh_offset"] + k * sh["sh_entsize"]
            st_name, st_info, _st_other, st_shndx, st_value, st_size = Elf64_Sym.unpack_from(
                buf, off
            )
            if (
                st_shndx == SHN_UNDEF
                or st_shndx == SHN_ABS
                or st_shndx == SHN_COMMON
                or st_shndx >= SHN_LORESERVE
                or st_name == 0
            ):
                continue
            end = strtab.find(b"\x00", st_name)
            if end < 0:
                continue
            sname = strtab[st_name:end].decode("utf-8", errors="replace")
            if not sname or sname.startswith("."):
                continue
            t = st_info & 0xF
            if t == STT_FILE:
                continue
            if t == STT_FUNC:
                kind = "func"
            elif t == STT_OBJECT:
                kind = "data"
            else:
                kind = "other"
            out.append(
                Symbol(
                    name=sname,
                    section_index=int(st_shndx),
                    offset=int(st_value),
                    size=int(st_size),
                    kind=kind,
                    binding=_bind_name(st_info),
                )
            )
    out.sort(key=lambda s: (s.section_index or 0, s.offset, s.name))
    return out


def _list_symbols_wasm(buf: bytes) -> list[Symbol]:
    """Export names from Wasm export section (best-effort)."""
    out: list[Symbol] = []
    if len(buf) < 8:
        return out
    # skip magic+version
    i = 8

    def read_u32() -> int:
        nonlocal i
        v = 0
        shift = 0
        while i < len(buf):
            b = buf[i]
            i += 1
            v |= (b & 0x7F) << shift
            if (b & 0x80) == 0:
                return v
            shift += 7
            if shift > 35:
                raise ValueError("leb overflow")
        raise ValueError("truncated")

    try:
        while i < len(buf):
            sid = buf[i]
            i += 1
            slen = read_u32()
            start = i
            end = i + slen
            if end > len(buf):
                break
            if sid == 7:  # export
                j = start
                nexp = 0
                # read count
                v = 0
                shift = 0
                while j < end:
                    b = buf[j]
                    j += 1
                    v |= (b & 0x7F) << shift
                    if (b & 0x80) == 0:
                        nexp = v
                        break
                    shift += 7
                for _ in range(nexp):
                    if j >= end:
                        break
                    nlen = buf[j]
                    j += 1
                    if j + nlen > end:
                        break
                    name = buf[j : j + nlen].decode("utf-8", errors="replace")
                    j += nlen
                    if j >= end:
                        break
                    kind = buf[j]
                    j += 1
                    # skip index leb
                    while j < end and buf[j] & 0x80:
                        j += 1
                    if j < end:
                        j += 1
                    out.append(
                        Symbol(
                            name=name,
                            section_index=None,
                            offset=0,
                            size=0,
                            kind="export" if kind == 0 else "other",
                            binding="export",
                        )
                    )
            i = end
    except ValueError:
        return out
    return out


def addr2line(buf: bytes, addr: int) -> list[Location]:
    """Map address → locations. DWARF via optional pyelftools; else enclosing symbol."""
    locs = _addr2line_dwarf(buf, addr)
    if locs:
        return locs
    if not elf.is_elf64_le(buf):
        return []
    # Enclosing FUNC in .text-relative space (ET_REL values are section offsets).
    best: Symbol | None = None
    for s in _list_symbols_elf(buf):
        if s.kind != "func" or s.size <= 0:
            continue
        if s.offset <= addr < s.offset + s.size:
            if best is None or s.offset >= best.offset:
                best = s
    if best is None:
        return []
    return [Location(path=best.name, line=None, role="sym")]


def _addr2line_dwarf(buf: bytes, addr: int) -> list[Location]:
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        return []
    try:
        ef = ELFFile(BytesIO(buf))
        if not ef.has_dwarf_info():
            return []
        dwarf = ef.get_dwarf_info()
        out: list[Location] = []
        for cu in dwarf.iter_CUs():
            lineprog = dwarf.line_program_for_CU(cu)
            if lineprog is None:
                continue
            prev = None
            for entry in lineprog.get_entries():
                state = entry.state
                if state is None:
                    continue
                if prev is not None and prev.address <= addr < state.address:
                    file_entry = lineprog["file_entry"][prev.file - 1]
                    path = file_entry.name
                    if isinstance(path, bytes):
                        path = path.decode("utf-8", errors="replace")
                    out.append(Location(path=str(path), line=int(prev.line), role="dwarf"))
                    return out
                if not state.end_sequence:
                    prev = state
    except Exception:
        return []
    return []


def locations_for_symbol(
    buf: bytes,
    name: str,
    *,
    source_files: dict[str, str] | None = None,
) -> list[Location]:
    locs: list[Location] = []
    for s in list_symbols(buf):
        if s.name != name:
            continue
        if s.kind == "func" and s.size > 0:
            locs.extend(addr2line(buf, s.offset))
        locs.append(Location(path=s.name, line=None, role="sym"))
        break
    if source_files:
        pat = re.compile(rf"\b{re.escape(name)}\s*\(")
        for path, text in source_files.items():
            for i, line in enumerate(text.splitlines(), 1):
                if pat.search(line):
                    role = "twin" if path.endswith(".py") else "def"
                    if path.endswith(".h"):
                        role = "decl"
                    locs.append(Location(path=path, line=i, role=role))
    # dedupe (path, line, role)
    seen: set[tuple[str, int | None, str]] = set()
    uniq: list[Location] = []
    for loc in locs:
        key = (loc.path, loc.line, loc.role)
        if key in seen:
            continue
        seen.add(key)
        uniq.append(loc)
    return uniq


def _section_by_index(buf: bytes, index: int) -> tuple[Optional[str], bytes]:
    for i, sh, name in _iter_shdrs(buf):
        if i == index:
            data = buf[sh["sh_offset"] : sh["sh_offset"] + sh["sh_size"]]
            return name, data
    return None, b""


def disasm(
    buf: bytes, section_index: int, offset: int = 0, limit: int = 64
) -> list[DisasmLine]:
    if elf.is_elf64_le(buf):
        name, data = _section_by_index(buf, section_index)
        chunk = data[offset : offset + limit]
        if name and name.startswith(".text"):
            lines = _disasm_capstone(chunk, offset, _eh_machine(buf))
            if lines:
                return lines
        return _disasm_db(chunk, offset)
    if len(buf) >= 4 and buf[:4] == b"\x00asm":
        # section_index is Wasm section list index from list_sections — walk
        return _disasm_wasm_code(buf, offset, limit)
    return []


def _disasm_db(data: bytes, base: int) -> list[DisasmLine]:
    out: list[DisasmLine] = []
    for i in range(0, len(data), 8):
        raw = data[i : i + 8]
        text = " ".join(f"{b:02x}" for b in raw)
        out.append(DisasmLine(addr=base + i, raw=raw, text=f"db {text}"))
    return out


def _disasm_capstone(data: bytes, base: int, em: int) -> list[DisasmLine]:
    try:
        from capstone import CS_ARCH_ARM64, CS_ARCH_X86, CS_MODE_64, Cs
    except ImportError:
        return []
    if em == EM_X86_64:
        md = Cs(CS_ARCH_X86, CS_MODE_64)
    elif em == EM_AARCH64:
        md = Cs(CS_ARCH_ARM64, CS_MODE_64)
    else:
        return []
    out: list[DisasmLine] = []
    for insn in md.disasm(data, base):
        raw = bytes(insn.bytes)
        out.append(DisasmLine(addr=insn.address, raw=raw, text=f"{insn.mnemonic} {insn.op_str}".strip()))
    return out


_WASM_OP = {
    0x0B: "end",
    0x10: "call",
    0x20: "local.get",
    0x21: "local.set",
    0x41: "i32.const",
    0x42: "i64.const",
    0x6A: "i32.add",
}


def _disasm_wasm_code(buf: bytes, offset: int, limit: int) -> list[DisasmLine]:
    # Find code section payload and disassemble a window of body bytes.
    i = 8

    def read_u32_at(pos: list[int]) -> int:
        v = 0
        shift = 0
        while pos[0] < len(buf):
            b = buf[pos[0]]
            pos[0] += 1
            v |= (b & 0x7F) << shift
            if (b & 0x80) == 0:
                return v
            shift += 7
        raise ValueError("truncated")

    try:
        pos = [i]
        while pos[0] < len(buf):
            sid = buf[pos[0]]
            pos[0] += 1
            slen = read_u32_at(pos)
            start = pos[0]
            if sid == 10:  # code
                body = buf[start : start + slen]
                chunk = body[offset : offset + limit]
                out: list[DisasmLine] = []
                j = 0
                while j < len(chunk):
                    op = chunk[j]
                    name = _WASM_OP.get(op, f"op_{op:02x}")
                    out.append(DisasmLine(addr=offset + j, raw=bytes([op]), text=name))
                    j += 1
                    if len(out) >= 64:
                        break
                return out
            pos[0] = start + slen
    except ValueError:
        pass
    return []


def mpy_disasm(mpy: bytes, limit: int = 80) -> list[DisasmLine]:
    """Basic .mpy dump: header + bytecode bytes as op_* lines."""
    out: list[DisasmLine] = []
    if len(mpy) < 4:
        return [DisasmLine(addr=0, raw=mpy, text="truncated mpy")]
    # MPY magic varies; show header words then raw ops.
    out.append(DisasmLine(addr=0, raw=mpy[:4], text=f"mpy_hdr {mpy[:4]!r}"))
    body = mpy[4:]
    n = min(len(body), limit)
    for i in range(n):
        b = body[i]
        out.append(DisasmLine(addr=4 + i, raw=bytes([b]), text=f"bc 0x{b:02x}"))
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("symbols")
    p.add_argument("path", type=Path)

    p = sub.add_parser("addr2line")
    p.add_argument("path", type=Path)
    p.add_argument("addr", type=lambda s: int(s, 0))

    p = sub.add_parser("locations")
    p.add_argument("path", type=Path)
    p.add_argument("name")

    p = sub.add_parser("disasm")
    p.add_argument("path", type=Path)
    p.add_argument("index", type=int)
    p.add_argument("offset", type=int, nargs="?", default=0)
    p.add_argument("limit", type=int, nargs="?", default=64)

    p = sub.add_parser("mpy")
    p.add_argument("path", type=Path)

    p = sub.add_parser("has-dwarf")
    p.add_argument("path", type=Path)

    args = ap.parse_args(argv)
    data = args.path.read_bytes()

    if args.cmd == "symbols":
        for s in list_symbols(data):
            print(f"{s.kind:6} {s.binding:6} +0x{s.offset:04x} sz={s.size:<5} {s.name}")
        return 0
    if args.cmd == "addr2line":
        for loc in addr2line(data, args.addr):
            ln = "" if loc.line is None else f":{loc.line}"
            print(f"{loc.role:6} {loc.path}{ln}")
        return 0
    if args.cmd == "disasm":
        for line in disasm(data, args.index, args.offset, args.limit):
            hx = line.raw.hex()
            print(f"0x{line.addr:04x}: {hx:<16} {line.text}")
        return 0
    if args.cmd == "mpy":
        for line in mpy_disasm(data):
            print(f"0x{line.addr:04x}: {line.text}")
        return 0
    if args.cmd == "has-dwarf":
        print("yes" if has_dwarf(data) else "no")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
