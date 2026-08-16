#!/usr/bin/env python3
"""Emit clangd compile_commands.json for extmod/wasmmod (crate-relative files)."""
import json
import pathlib
import sys

wasm = pathlib.Path(sys.argv[1]).resolve()
ws = pathlib.Path(sys.argv[2]).resolve()
vscode_cdb = pathlib.Path(sys.argv[3]) if len(sys.argv) > 3 else None
mpwm = wasm.parent.parent

skip = {"target", "build", "third_party", ".git"}
# -I must not depend on CDB directory: clangd treats "." as the workspace,
# then py/obj.h is missing and MP_REGISTER_ROOT_POINTER looks like implicit int.
host_cflags = (
    "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes "
    f"-I{wasm} -I{wasm / 'src'} -I{wasm / 'third_party/wamr/core/iwasm/include'} "
    f"-I{mpwm} -I{mpwm / 'ports/unix'} -I{mpwm / 'ports/unix/variants/standard'} "
    f"-I{mpwm / 'ports/unix/build-wasm'} -DMICROPY_PY_WASM=1 "
    "-DMICROPY_MODULE_BUILTIN_INIT=1 -DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1 "
    "-DPM_WASMMOD_GUEST=0"
)
upy_cflags = (
    "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes "
    f"-I{wasm} -I{wasm / 'src'} -I{wasm / 'third_party/wamr/core/iwasm/include'} "
    f"-I{mpwm} -I{mpwm / 'ports/unix'} -I{mpwm / 'ports/unix/variants/standard'} "
    f"-I{mpwm / 'ports/unix/build-metal'} -DMICROPY_PY_WASM=1 "
    "-DMICROPY_MODULE_BUILTIN_INIT=1 -DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1 "
    "-DPM_WASMMOD_GUEST=0"
    f" -include {wasm / 'ports/micropython/mpconfig_wasm.h'}"
)
guest_cflags = (
    "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes "
    f"-I{wasm} -I{wasm / 'src'} -DPM_WASMMOD_GUEST=1"
)
cpy_cflags = (
    "clang -xc -std=gnu11 -Wall -Wno-unknown-attributes "
    f"-I{wasm} -I{wasm / 'src'} -I{wasm / 'ports/cpython/stubs'} -I{mpwm} "
    f"-I{mpwm / 'lib/uzlib'} "
    "-I/usr/include/python3.12 -DPM_WASMMOD_CPYTHON=1 -DPM_WASMMOD_GUEST=0 "
    "-DMICROPY_PY_DEFLATE=1"
)
elf_files = {"packbind.c", "packbind.h", "finder.c", "finder.h"}


def rel(p: pathlib.Path) -> str:
    return p.relative_to(wasm).as_posix()


def flags_for(p: pathlib.Path) -> str:
    parts = p.parts
    name = p.name
    if "examples" in parts:
        return guest_cflags
    if "ports/cpython" in "/".join(p.relative_to(wasm).parts):
        return cpy_cflags
    extra = " -DMICROPY_PY_WASM_ELF=1" if name in elf_files else ""
    if "ports/micropython" in "/".join(p.relative_to(wasm).parts):
        return upy_cflags + extra
    return host_cflags + extra


ents = []
for f in sorted(wasm.rglob("*")):
    if f.suffix not in {".c", ".h"}:
        continue
    if any(s in f.parts for s in skip):
        continue
    r = rel(f)
    ents.append(
        {
            # Absolute crate dir: clangd resolves "." as the workspace, so
            # -I../../. never reaches py/obj.h and MP_REGISTER_ROOT_POINTER
            # is parsed as implicit int. File paths stay crate-relative.
            "directory": str(wasm),
            "file": r,
            "command": flags_for(f) + " -c " + r,
        }
    )

(wasm / "compile_commands.json").write_text(json.dumps(ents, indent=4) + "\n")

if vscode_cdb is not None:
    vscode_cdb.parent.mkdir(parents=True, exist_ok=True)
    data = json.loads(vscode_cdb.read_text()) if vscode_cdb.exists() else []
    marker = "packages/micropython-wasmmod/extmod/wasmmod/"
    kept = []
    for e in data:
        f = str(e.get("file", ""))
        d = str(e.get("directory", ""))
        if marker in f or marker in d or f.startswith("ports/") and "wasmmod" in d:
            continue
        if "extmod/wasmmod/" in f.replace("\\", "/"):
            continue
        kept.append(e)
    vents = []
    for e in ents:
        vents.append(
            {
                "directory": e["directory"],
                "file": str(wasm / e["file"]),
                "command": e["command"],
            }
        )
    vscode_cdb.write_text(json.dumps(vents + kept, indent=4) + "\n")

print("wasmmod clangd TUs", len(ents))
