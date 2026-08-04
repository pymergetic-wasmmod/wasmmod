/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * MicroPython webassembly (Emscripten) platform defaults for wasmmod.
 *
 * Include from the out-of-tree variant:
 *   ports/micropython/webassembly/variant/mpconfigvariant.h
 *
 * Build: make -C ports/micropython/webassembly  (WASMMOD_EMSCRIPTEN=1).
 * ASYNCIFY is required so js.fetch can park inside sync mp_wasm_io_ops_t.
 */
#ifndef MICROPY_INCLUDED_WASMMOD_MPCONFIG_WEBASSEMBLY_H
#define MICROPY_INCLUDED_WASMMOD_MPCONFIG_WEBASSEMBLY_H

#ifndef MICROPY_WASM_HTTP_NATIVE
#define MICROPY_WASM_HTTP_NATIVE (0)
#endif

#include "../../../io.h"

extern const mp_wasm_io_ops_t mp_wasm_io_browser;
#define MICROPY_WASM_IO_OPS mp_wasm_io_browser

#endif // MICROPY_INCLUDED_WASMMOD_MPCONFIG_WEBASSEMBLY_H
