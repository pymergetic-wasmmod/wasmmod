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
Build a freestanding Wasm guest and optionally append a MicroPython pack
section (`micropython.pack`). See examples/PACK.md (this repo).

Examples:
  tools/wasm_pack.py hello.c -o hello.wasm --export hello --export add
  tools/wasm_pack.py hello.c -o hello.wasm --name hello --mount py \\
      --export hello --export add
  tools/wasm_pack.py examples/hello -o hello.wasm
  tools/wasm_pack.py examples/hello/pack.toml -o hello.wasm
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

SECTION_NAME = "micropython.pack"
IMPORTS_SECTION = "micropython.imports"
MAGIC = b"MPWP"
IMPORTS_MAGIC = b"MPWI"
PACK_VERSION_V1 = 1
PACK_VERSION_V2 = 2
IMPORTS_VERSION = 1

KIND_PY = 1
KIND_MPY = 2
KIND_RAW = 3

SIG_AUTO = 255
C_EXTS = {".c"}
CXX_EXTS = {".cc", ".cpp", ".cxx", ".C"}
RS_EXTS = {".rs"}


def sig_tag(sig: str | None) -> int:
    """Map pack.toml sig strings to binder tags.

    Loader introspects real Wasm types (i32/i64/f32/f64). Keep optional
    legacy N-i32 tags for old readers; anything richer → SIG_AUTO.
    """
    if not sig:
        return SIG_AUTO
    parts = [p for p in sig.split("_") if p]
    if not parts:
        return SIG_AUTO
    if any(p not in ("i32",) for p in parts):
        return SIG_AUTO
    # all i32: optional compact tag (result is last)
    nparams = len(parts) - 1
    if 0 <= nparams <= 8 and parts[-1] == "i32":
        return nparams
    return SIG_AUTO


def load_toml(path: Path) -> dict:
    try:
        import tomllib  # Python 3.11+
    except ImportError:
        try:
            import tomli as tomllib  # type: ignore
        except ImportError as e:
            raise SystemExit(
                "wasm_pack: need Python 3.11+ (tomllib) or the tomli package to read pack.toml"
            ) from e
    with path.open("rb") as f:
        return tomllib.load(f)


def find_wasm_ld() -> str | None:
    env = os.environ.get("WASM_LD")
    if env and Path(env).is_file():
        return env
    for candidate in (
        shutil.which("wasm-ld"),
        shutil.which("wasm-ld-18"),
        shutil.which("wasm-ld-17"),
    ):
        if candidate:
            return candidate
    # Do not Path.resolve() wasm-ld: wasi-sdk's wasm-ld → lld symlink breaks
    # clang's -fuse-ld driver selection.
    wasi_ld = (
        Path(__file__).resolve().parents[1]
        / ".."
        / "metal"
        / ".tools"
        / "wasi-sdk"
        / "bin"
        / "wasm-ld"
    )
    if wasi_ld.exists():
        return str(wasi_ld)
    rustup = Path.home() / ".rustup" / "toolchains"
    if rustup.is_dir():
        for ld in rustup.glob("*/lib/rustlib/*/bin/gcc-ld/wasm-ld"):
            return str(ld)
    return None


def find_clang() -> str:
    env = os.environ.get("WASM_CC") or os.environ.get("CC_WASM")
    if env:
        return env
    wasi_clang = (
        Path(__file__).resolve().parents[1]
        / ".."
        / "metal"
        / ".tools"
        / "wasi-sdk"
        / "bin"
        / "clang"
    )
    if wasi_clang.exists():
        return str(wasi_clang)
    clang = shutil.which("clang")
    if not clang:
        raise SystemExit("wasm_pack: clang not found (set WASM_CC)")
    return clang


def find_clangxx(clang: str) -> str:
    env = os.environ.get("WASM_CXX") or os.environ.get("CXX_WASM")
    if env:
        return env
    cand = Path(clang).with_name(Path(clang).name.replace("clang", "clang++"))
    if cand.exists():
        return str(cand)
    cxx = shutil.which("clang++")
    if not cxx:
        raise SystemExit("wasm_pack: clang++ not found (set WASM_CXX) for C++ sources")
    return cxx


def find_rustc() -> str:
    env = os.environ.get("WASM_RUSTC") or os.environ.get("RUSTC")
    if env:
        return env
    rustc = shutil.which("rustc")
    if not rustc:
        raise SystemExit("wasm_pack: rustc not found (set WASM_RUSTC) for Rust sources")
    return rustc


def uleb128(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def kind_for_path(path: str) -> int:
    if path.endswith(".py"):
        return KIND_PY
    if path.endswith(".mpy"):
        return KIND_MPY
    return KIND_RAW


def find_mpy_cross() -> str | None:
    env = os.environ.get("MPY_CROSS")
    if env and Path(env).is_file():
        return env
    here = Path(__file__).resolve()
    # tools/ → wasmmod → extmod → repo root (typical submodule layout)
    for parent in here.parents:
        for cand in (
            parent / "mpy-cross" / "build" / "mpy-cross",
            parent / "mpy-cross" / "mpy-cross",
        ):
            if cand.is_file() and os.access(cand, os.X_OK):
                return str(cand)
    return shutil.which("mpy-cross")


def freeze_py_to_mpy(rel: str, data: bytes, mpy_cross: str, opt: str) -> bytes:
    """Compile .py source bytes to .mpy via mpy-cross."""
    import tempfile

    with tempfile.TemporaryDirectory(prefix="wasm_pack_mpy_") as td:
        td_path = Path(td)
        # Preserve basename so -s embedding stays readable.
        base = Path(rel).name
        if not base.endswith(".py"):
            base = base + ".py"
        py_path = td_path / base
        mpy_path = td_path / (Path(base).stem + ".mpy")
        py_path.write_bytes(data)
        cmd = [
            mpy_cross,
            f"-O{opt}",
            "-o",
            str(mpy_path),
            "-s",
            rel,
            str(py_path),
        ]
        print("+", " ".join(cmd), file=sys.stderr)
        try:
            subprocess.check_call(cmd)
        except subprocess.CalledProcessError as e:
            raise SystemExit(f"wasm_pack: mpy-cross failed for {rel}") from e
        return mpy_path.read_bytes()


def collect_mounts(
    mount_dirs: list[Path],
    *,
    freeze: bool = False,
    mpy_cross: str | None = None,
    opt: str = "2",
) -> list[tuple[str, int, bytes]]:
    files: list[tuple[str, int, bytes]] = []
    if freeze and not mpy_cross:
        raise SystemExit(
            "wasm_pack: freeze requires mpy-cross "
            "(build mpy-cross, or set MPY_CROSS=/path/to/mpy-cross)"
        )
    for root in mount_dirs:
        root = root.resolve()
        if not root.is_dir():
            raise SystemExit(f"wasm_pack: --mount not a directory: {root}")
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            if rel.startswith("../") or "/../" in f"/{rel}/":
                raise SystemExit(f"wasm_pack: refusing path {rel}")
            data = path.read_bytes()
            kind = kind_for_path(rel)
            if freeze and kind == KIND_PY:
                data = freeze_py_to_mpy(rel, data, mpy_cross, opt)  # type: ignore[arg-type]
                rel = rel[: -3] + ".mpy" if rel.endswith(".py") else rel + ".mpy"
                kind = KIND_MPY
            files.append((rel, kind, data))
    return files


def build_pack_payload(
    name: str,
    files: list[tuple[str, int, bytes]],
    exports: list[tuple[str, str, str, int]] | None = None,
) -> bytes:
    """exports entries: (module, func, export_name, sig_tag)."""
    name_b = name.encode("utf-8")
    if len(name_b) > 0xFFFF:
        raise SystemExit("wasm_pack: package name too long")
    use_v2 = bool(exports)
    out = bytearray()
    out += MAGIC
    out += struct.pack("<HH", PACK_VERSION_V2 if use_v2 else PACK_VERSION_V1, 0)
    out += struct.pack("<H", len(name_b))
    out += name_b
    out += struct.pack("<I", len(files))
    for rel, kind, data in files:
        rel_b = rel.encode("utf-8")
        if len(rel_b) > 0xFFFF:
            raise SystemExit(f"wasm_pack: path too long: {rel}")
        if len(data) > 0xFFFFFFFF:
            raise SystemExit(f"wasm_pack: file too large: {rel}")
        out += struct.pack("<H", len(rel_b))
        out += rel_b
        out += struct.pack("<B", kind)
        out += struct.pack("<I", len(data))
        out += data
    if use_v2:
        assert exports is not None
        out += struct.pack("<I", len(exports))
        for module, func, export_name, sig in exports:
            for label, s in (("module", module), ("func", func), ("export", export_name)):
                b = s.encode("utf-8")
                if len(b) > 0xFFFF:
                    raise SystemExit(f"wasm_pack: {label} too long: {s}")
                out += struct.pack("<H", len(b))
                out += b
            out += struct.pack("<B", sig & 0xFF)
    return bytes(out)


def append_custom_section(wasm: bytes, section_name: str, payload: bytes) -> bytes:
    name_b = section_name.encode("utf-8")
    body = uleb128(len(name_b)) + name_b + payload
    return wasm + bytes([0]) + uleb128(len(body)) + body


def build_imports_payload(imports: list[tuple[str, str]]) -> bytes:
    out = bytearray()
    out += IMPORTS_MAGIC
    out += struct.pack("<H", IMPORTS_VERSION)
    out += struct.pack("<I", len(imports))
    for module, func in imports:
        for label, s in (("module", module), ("func", func)):
            b = s.encode("utf-8")
            if len(b) > 0xFFFF:
                raise SystemExit(f"wasm_pack: import {label} too long: {s}")
            out += struct.pack("<H", len(b))
            out += b
    return bytes(out)


def compile_to_obj(src: Path, obj: Path, opt: str) -> None:
    ext = src.suffix
    if ext in C_EXTS:
        clang = find_clang()
        cmd = [
            clang,
            f"-O{opt}",
            "--target=wasm32",
            "-nostdlib",
            "-ffreestanding",
            "-c",
            "-o",
            str(obj),
            str(src),
        ]
    elif ext in CXX_EXTS:
        cxx = find_clangxx(find_clang())
        cmd = [
            cxx,
            f"-O{opt}",
            "--target=wasm32",
            "-nostdlib",
            "-ffreestanding",
            "-fno-exceptions",
            "-fno-rtti",
            "-c",
            "-o",
            str(obj),
            str(src),
        ]
    elif ext in RS_EXTS:
        rustc = find_rustc()
        cmd = [
            rustc,
            f"-Copt-level={opt}",
            "--target=wasm32-unknown-unknown",
            "--crate-type=lib",
            "--emit=obj",
            "-Cpanic=abort",
            "-o",
            str(obj),
            str(src),
        ]
    else:
        raise SystemExit(f"wasm_pack: unsupported source type: {src}")
    print("+", " ".join(cmd), file=sys.stderr)
    subprocess.check_call(cmd)


def compile_wasm(sources: list[str], out: Path, exports: list[str], opt: str) -> None:
    """Compile mixed C/C++/Rust sources and link a freestanding .wasm."""
    clang = find_clang()
    wasm_ld = find_wasm_ld()
    src_paths = [Path(s) for s in sources]
    # Fast path: single C file can still go through the clang driver.
    if len(src_paths) == 1 and src_paths[0].suffix in C_EXTS:
        cmd = [
            clang,
            f"-O{opt}",
            "--target=wasm32",
            "-nostdlib",
            "-ffreestanding",
            "-Wl,--no-entry",
            "-Wl,--allow-undefined",
        ]
        if exports:
            for sym in exports:
                cmd.append(f"-Wl,--export={sym}")
        else:
            cmd.append("-Wl,--export-all")
        if wasm_ld:
            cmd.append(f"-fuse-ld={wasm_ld}")
        cmd.extend(["-o", str(out), str(src_paths[0])])
        print("+", " ".join(cmd), file=sys.stderr)
        subprocess.check_call(cmd)
        return

    objs: list[Path] = []
    try:
        for src in src_paths:
            obj = out.with_name(f".{out.stem}.{src.stem}{src.suffix}.o")
            compile_to_obj(src, obj, opt)
            objs.append(obj)
        if wasm_ld:
            cmd = [wasm_ld, "--no-entry", "--allow-undefined", "-o", str(out)]
        else:
            cmd = [
                clang,
                "--target=wasm32",
                "-nostdlib",
                "-Wl,--no-entry",
                "-Wl,--allow-undefined",
                "-o",
                str(out),
            ]
        if exports:
            for sym in exports:
                flag = f"--export={sym}" if wasm_ld else f"-Wl,--export={sym}"
                cmd.append(flag)
        else:
            cmd.append("--export-all" if wasm_ld else "-Wl,--export-all")
        cmd.extend(str(o) for o in objs)
        print("+", " ".join(cmd), file=sys.stderr)
        subprocess.check_call(cmd)
    finally:
        for o in objs:
            try:
                o.unlink()
            except OSError:
                pass


def resolve_pack_root(arg: Path) -> tuple[Path, Path] | None:
    """If arg is a pack dir or pack.toml, return (root, pack.toml path)."""
    if arg.is_file() and arg.name == "pack.toml":
        return arg.parent.resolve(), arg.resolve()
    if arg.is_dir():
        manifest = arg / "pack.toml"
        if manifest.is_file():
            return arg.resolve(), manifest.resolve()
    return None


def manifest_to_build(
    root: Path, data: dict
) -> tuple[
    list[str],
    list[str],
    list[tuple[str, str, str, int]],
    list[tuple[str, str]],
    list[Path],
    str,
    bool | None,
]:
    """Return (sources, link_exports, pack_exports, imports, mounts, name, freeze).

    freeze is None when [python].freeze is omitted (CLI / default apply).
    """
    name = data.get("name")
    if not name or not isinstance(name, str):
        raise SystemExit("wasm_pack: pack.toml missing string 'name'")

    native = data.get("native") or {}
    sources: list[str] = []
    listed = native.get("sources")
    if listed:
        if not isinstance(listed, list) or not all(isinstance(s, str) for s in listed):
            raise SystemExit("wasm_pack: native.sources must be a list of strings")
        for s in listed:
            p = (root / s).resolve()
            if not p.is_file():
                raise SystemExit(f"wasm_pack: source not found: {p}")
            sources.append(str(p))
    else:
        ndir = native.get("dir", "native")
        if not isinstance(ndir, str):
            raise SystemExit("wasm_pack: native.dir must be a string")
        d = (root / ndir).resolve()
        if d.is_dir():
            for ext in ("*.c", "*.cc", "*.cpp", "*.cxx", "*.rs"):
                for p in sorted(d.glob(ext)):
                    sources.append(str(p))
        if not sources:
            raise SystemExit(
                f"wasm_pack: no native sources (set native.sources or put .c/.cpp/.rs under {ndir}/)"
            )

    link_exports: list[str] = []
    pack_exports: list[tuple[str, str, str, int]] = []
    for item in data.get("exports") or []:
        if not isinstance(item, dict):
            continue
        func = item.get("func")
        if not isinstance(func, str) or not func:
            continue
        export_name = item.get("export", func)
        if not isinstance(export_name, str) or not export_name:
            export_name = func
        module = item.get("module", "")
        if module is None:
            module = ""
        if not isinstance(module, str):
            raise SystemExit("wasm_pack: exports.module must be a string")
        if module == ".":
            module = ""
        sig = item.get("sig")
        sig_s = sig if isinstance(sig, str) else None
        pack_exports.append((module, func, export_name, sig_tag(sig_s)))
        if export_name not in link_exports:
            link_exports.append(export_name)

    life = data.get("lifecycle") or {}
    for key in ("load", "unload"):
        fn = life.get(key)
        if isinstance(fn, str) and fn and fn not in link_exports:
            link_exports.append(fn)

    imports: list[tuple[str, str]] = []
    for item in data.get("imports") or []:
        if not isinstance(item, dict):
            continue
        mod = item.get("module")
        func = item.get("func")
        if isinstance(mod, str) and mod and isinstance(func, str) and func:
            imports.append((mod, func))

    mounts: list[Path] = []
    py = data.get("python") or {}
    mount = py.get("mount", "py")
    if isinstance(mount, str) and mount:
        m = (root / mount).resolve()
        if m.is_dir():
            mounts.append(m)

    freeze: bool | None = None
    if "freeze" in py:
        freeze = bool(py.get("freeze"))

    return sources, link_exports, pack_exports, imports, mounts, name, freeze


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument(
        "inputs",
        nargs="+",
        help="C sources, a pack directory, or pack.toml",
    )
    ap.add_argument("-o", "--output", required=True, help="Output .wasm path")
    ap.add_argument("--export", action="append", default=[], help="Export symbol (repeatable)")
    ap.add_argument("-O", default="2", help="Optimization level (default 2)")
    ap.add_argument("--name", help="Package name for micropython.pack (default: output stem)")
    ap.add_argument(
        "--mount",
        action="append",
        default=[],
        type=Path,
        help="Directory of .py/.mpy/assets to embed (repeatable)",
    )
    ap.add_argument(
        "--freeze",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="Compile mounted .py to .mpy before embed (default: off / source-only; pack.toml [python].freeze)",
    )
    ap.add_argument(
        "--mpy-cross",
        default=os.environ.get("MPY_CROSS"),
        help="mpy-cross binary (default: MPY_CROSS or discover next to host tree)",
    )
    ap.add_argument(
        "--no-pack-section",
        action="store_true",
        help="Do not append micropython.pack even if --mount/--name given",
    )
    ap.add_argument(
        "--write-mpack",
        nargs="?",
        const="",
        default=None,
        help="Write raw micropython.pack payload to PATH (default: <out>.mpack)",
    )
    ap.add_argument(
        "--aot",
        nargs="?",
        const="",
        default=None,
        help="Run wamrc to produce PATH (default: <out>.aot); host-specific",
    )
    ap.add_argument("--wamrc", default=os.environ.get("WAMRC", "wamrc"), help="wamrc binary")
    args = ap.parse_args()

    sources: list[str] = []
    link_exports: list[str] = list(args.export)
    pack_exports: list[tuple[str, str, str, int]] = []
    pack_imports: list[tuple[str, str]] = []
    mounts: list[Path] = list(args.mount)
    pkg_name: str | None = args.name
    # Default: source-only (.py). Opt in via pack.toml freeze=true or --freeze.
    freeze: bool | None = args.freeze
    manifest_freeze: bool | None = None

    for inp in args.inputs:
        path = Path(inp)
        resolved = resolve_pack_root(path)
        if resolved is not None:
            root, manifest = resolved
            data = load_toml(manifest)
            m_sources, m_link, m_pack, m_imports, m_mounts, m_name, m_freeze = manifest_to_build(
                root, data
            )
            sources.extend(m_sources)
            for e in m_link:
                if e not in link_exports:
                    link_exports.append(e)
            pack_exports.extend(m_pack)
            pack_imports.extend(m_imports)
            for m in m_mounts:
                if m not in mounts:
                    mounts.append(m)
            if pkg_name is None:
                pkg_name = m_name
            if m_freeze is not None:
                manifest_freeze = m_freeze
            print(f"manifest {manifest}", file=sys.stderr)
            continue
        if path.is_file():
            sources.append(str(path.resolve()))
            continue
        raise SystemExit(f"wasm_pack: not a source, pack dir, or pack.toml: {inp}")

    if freeze is None:
        freeze = False if manifest_freeze is None else manifest_freeze

    # CLI --export without pack.toml metadata → auto-arity table entries.
    if args.export and not pack_exports:
        for e in args.export:
            pack_exports.append(("", e, e, SIG_AUTO))

    if not sources:
        raise SystemExit("wasm_pack: no sources to compile")

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    compile_wasm(sources, out, link_exports, args.O)

    raw = out.read_bytes()
    want_section = (not args.no_pack_section) and (mounts or pkg_name or pack_exports)
    if want_section:
        name = pkg_name or out.stem
        mpy_cross = args.mpy_cross or find_mpy_cross()
        files = (
            collect_mounts(mounts, freeze=freeze, mpy_cross=mpy_cross, opt=args.O)
            if mounts
            else []
        )
        n_mpy = sum(1 for _, k, _ in files if k == KIND_MPY)
        n_py = sum(1 for _, k, _ in files if k == KIND_PY)
        payload = build_pack_payload(name, files, pack_exports or None)
        raw = append_custom_section(raw, SECTION_NAME, payload)
        print(
            f"packed section {SECTION_NAME!r}: name={name!r} files={len(files)} "
            f"(py={n_py} mpy={n_mpy} freeze={freeze}) "
            f"exports={len(pack_exports)} payload={len(payload)}B",
            file=sys.stderr,
        )
    if pack_imports:
        ipayload = build_imports_payload(pack_imports)
        raw = append_custom_section(raw, IMPORTS_SECTION, ipayload)
        print(
            f"packed section {IMPORTS_SECTION!r}: imports={len(pack_imports)} payload={len(ipayload)}B",
            file=sys.stderr,
        )
    if want_section or pack_imports:
        out.write_bytes(raw)
    else:
        raw = out.read_bytes()

    if args.write_mpack is not None:
        mpack_path = Path(args.write_mpack) if args.write_mpack else out.with_suffix(".mpack")
        payload = extract_custom_section(raw, SECTION_NAME)
        if payload is None:
            raise SystemExit("wasm_pack: no micropython.pack section to write")
        mpack_path.write_bytes(payload)
        print(f"wrote {mpack_path} ({len(payload)}B)", file=sys.stderr)

    if args.aot is not None:
        aot_path = Path(args.aot) if args.aot else out.with_suffix(".aot")
        wamrc = shutil.which(args.wamrc) or args.wamrc
        cmd = [wamrc, "-o", str(aot_path), str(out)]
        print("+", " ".join(cmd), file=sys.stderr)
        try:
            subprocess.check_call(cmd)
        except (OSError, subprocess.CalledProcessError) as e:
            raise SystemExit(
                f"wasm_pack: wamrc failed ({e}); install/build wamrc for --aot"
            ) from e
        print(aot_path)

    print(out)
    return 0


def extract_custom_section(wasm: bytes, section_name: str) -> bytes | None:
    if len(wasm) < 8 or wasm[:4] != b"\x00asm":
        return None
    name_b = section_name.encode("utf-8")
    i = 8
    while i < len(wasm):
        sid = wasm[i]
        i += 1
        size, i = _read_uleb(wasm, i)
        sec_end = i + size
        if sid == 0:
            nlen, j = _read_uleb(wasm, i)
            if wasm[j : j + nlen] == name_b:
                return wasm[j + nlen : sec_end]
        i = sec_end
    return None


def _read_uleb(buf: bytes, i: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while i < len(buf):
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            return result, i
        shift += 7
    raise SystemExit("wasm_pack: truncated wasm")


if __name__ == "__main__":
    raise SystemExit(main())
