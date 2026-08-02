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
| `docs/PACK.md` | Pack / imports section format |
| `examples/` | Guest packs + call-matrix smoke (`hello`, `client`, `mixed`, `bridge`) |
| `tools/` | Host-agnostic pack/sign CLIs (`wasm_pack.py`, `wasm_sign.py`) |
| `third_party/wamr` | Nested [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime) submodule |

Planned split (non-breaking): `core/` + `hosts/micropython/` (+ later
`hosts/cpython/`). Pack section names are still MicroPython-branded
(`micropython.pack`, `MPWP`); a neutral ABI can follow once a second host lands.

## MicroPython integration

```bash
# from a MicroPython / metalpython tree
git submodule add https://github.com/pymergetic/wasmmod.git extmod/wasmmod
git submodule update --init --recursive extmod/wasmmod
```

Enable with `MICROPY_PY_WASM=1` (and optional `MICROPY_PY_WASM_{AOT,JIT,FAST_JIT}`).
WAMR lives at `third_party/wamr` (nested). Host glue still cmake-builds it
(`extmod/extmod.mk` / `extmod.cmake`).

## Examples

From a MicroPython / metalpython tree with this repo as `extmod/wasmmod`:

```bash
make -C extmod/wasmmod/examples test
```

## License

MIT — Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
