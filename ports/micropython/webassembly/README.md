# MicroPython webassembly platform (Emscripten / browser)

Browser host glue for wasmmod. **Nothing wasmmod-specific needs to live in
metalpython's `ports/webassembly/`** — use the out-of-tree variant + this Makefile.

## Build (vanilla metalpython port tree)

```bash
source ~/emsdk/emsdk_env.sh
# from metalpython root (submodule at extmod/wasmmod):
make -C extmod/wasmmod/ports/micropython/webassembly -j$(nproc)
# → ports/webassembly/build-wasmmod/micropython.mjs (+ .wasm)
# Defaults: MICROPY_WASM_VERIFY=1 + trust CA from examples/.keys (sign-key).
#   MICROPY_WASM_VERIFY=0 make -C …/webassembly   # unsigned smoke
#
# Browser builds force MICROPY_PY_WASM_ELF=0 and MICROPY_WASM_CONTAINERS=wasm
# (no ET_REL execute under Emscripten). Ship/serve `.wasm` only for the REPL.
```

Equivalent manual invoke:

```bash
make -C ports/webassembly \
  VARIANT_DIR=$PWD/extmod/wasmmod/ports/micropython/webassembly/variant \
  BUILD=$PWD/ports/webassembly/build-wasmmod
```

## Layout

| Path | Role |
|------|------|
| `io_browser.c` | `mp_wasm_io_browser` — GET/HEAD via `js.fetch` (Bearer + `X-Shell-Session-Id` when set) |
| `mpconfig_webassembly.h` | `MICROPY_WASM_HTTP_NATIVE=0` + `MICROPY_WASM_IO_OPS` |
| `wamr_em_wasi_shim.h` | WASI typedef clash under emcc |
| `variant/` | Out-of-tree `mpconfigvariant.{h,mk}` for µPy `VARIANT_DIR=` |
| `Makefile` | Wrapper → `ports/webassembly` with `VARIANT_DIR` |

Asyncify note: stock µPy `runPythonAsync` does not pass `{ async: true }` to
`ccall`. metal-cdn `repl.js` wraps it using `mp._module` so pack `import` can
park on `js.fetch` without patching metalpython `api.js`.

See [ports/PORT.md](../../PORT.md).
