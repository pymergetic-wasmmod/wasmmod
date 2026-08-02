# wasmmod

Guest WASM pack loader for Python runtimes — load signed `.wasm` packs via
[WAMR](https://github.com/bytecodealliance/wasm-micro-runtime), with a
MicroPython host first and a path to CPython.

**Status:** extracted from metalpython / MicroPython `extmod/wasmmod`. Layout is
drop-in for a git submodule at `extmod/wasmmod`
(`#include "extmod/wasmmod/..."` unchanged).

## Layout

| Path | Role |
|------|------|
| `pack.*`, `runtime.*`, `forward.*`, `verify.*` | Portable core (pack format, WAMR load, guest→guest forwarders, trust) |
| `fetch.*` | Byte loader (currently MicroPython reader-backed) |
| `wasmmod.c`, `finder.*`, `host.*` | MicroPython host (`wasm` module, import hook, host slots/handles) |
| `ports/micropython/` | Make/CMake fragments + optional `mpconfig_wasm.h` |
| `ports/cpython/` | (planned) CPython extension glue — port picks `.pyc` / `.py` |
| `docs/PACK.md` | Pack / imports section format |
| `examples/` | Guest packs + call-matrix smoke (`hello`, `client`, `mixed`, `bridge`) |
| `tools/` | Host-agnostic pack/sign CLIs (`wasm_pack.py`, `wasm_sign.py`) |
| `third_party/wamr` | Nested [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime) submodule |

## MicroPython integration

```bash
git submodule add https://github.com/pymergetic/wasmmod.git extmod/wasmmod
git submodule update --init --recursive extmod/wasmmod
```

Host tree only needs thin includes (no `py/mpconfig.h` / `mpconfigport.mk` edits):

```make
# extmod/extmod.mk
ifeq ($(MICROPY_PY_WASM),1)
include $(TOP)/extmod/wasmmod/ports/micropython/micropython.mk
endif
```

```cmake
# extmod/extmod.cmake
if(MICROPY_PY_WASM)
    include(${MICROPY_DIR}/extmod/wasmmod/ports/micropython/micropython.cmake)
endif()
```

Enable with `make MICROPY_PY_WASM=1` (optional `MICROPY_PY_WASM_{AOT,JIT,FAST_JIT,MATRIX}`).
Optional C defaults: `#include "extmod/wasmmod/ports/micropython/mpconfig_wasm.h"`
from a port `mpconfigport.h` — not required when using the make fragment.

Optional host trampolines (metalpython): `tools/wasm_pack.py` / `tools/wasm_sign.py`
→ `extmod/wasmmod/tools/…`.

## Build & test (unix host)

From the **host** MicroPython / metalpython tree (submodule already at
`extmod/wasmmod`). Cold worktree once:

```bash
make -C ports/unix submodules
make -C mpy-cross BUILD=build -j"$(nproc)"   # needed for pack freeze (.mpy)
```

Then:

```bash
# Interpreter matrix (packs + build-wasm + run_matrix.py)
make -C extmod/wasmmod/examples test

# All engines: interp + AOT + Fast JIT (+ LLVM JIT if linkable)
make -C extmod/wasmmod/examples test-engines

# Same via metalpython symlink (if present)
make -C examples/wasmmod test-engines
```

Details, manual smoke, and pack.toml notes: [examples/README.md](examples/README.md).
Pack format: [docs/PACK.md](docs/PACK.md).

## License

MIT — Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
