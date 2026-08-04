# Enable wasmmod for MicroPython ports/webassembly (out-of-tree VARIANT_DIR).
MICROPY_PY_WASM = 1
WASMMOD_EMSCRIPTEN = 1

# Asyncify so js.fetch can park inside sync I/O ops.
JSFLAGS += -s ASYNCIFY

CFLAGS += -DMICROPY_WASM_HTTP_NATIVE=0
