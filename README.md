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
| `ports/micropython/` | Make/CMake fragments for MicroPython hosts |
| `ports/cpython/` | (planned) CPython extension glue |
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

## Examples

```bash
make -C extmod/wasmmod/examples test
```

## License

MIT — Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
