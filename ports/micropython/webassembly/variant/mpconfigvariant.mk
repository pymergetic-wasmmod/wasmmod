# Enable wasmmod for MicroPython ports/webassembly (out-of-tree VARIANT_DIR).
MICROPY_PY_WASM = 1
WASMMOD_EMSCRIPTEN = 1

# Asyncify so js.fetch can park inside sync I/O ops.
# Default stack is tight for REPL eval + catalog/import fetch; 64KiB is safer.
JSFLAGS += -s ASYNCIFY
JSFLAGS += -s ASYNCIFY_STACK_SIZE=65536

CFLAGS += -DMICROPY_WASM_HTTP_NATIVE=0

# Pack signature verify (ECDSA-P256 via mbedtls) + baked demo root CA.
MICROPY_PY_SSL = 1
MICROPY_SSL_MBEDTLS = 1
MICROPY_WASM_VERIFY ?= 1
# Default: examples PKI from `make -C examples sign-key` / sign-packs.
MICROPY_WASM_TRUST_CA ?= $(TOP)/extmod/wasmmod/examples/.keys/trust/root.crt.der
