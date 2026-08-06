/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Metal port: replaceable mp_wasm_io_ops_t (async→sync park inside fetch/probe).
 * Stub DECLINEs all URIs so unix defaults remain until Metal fills the hooks.
 *
 * Call mp_wasm_metal_io_ops_init() before mp_wasm_io_set(&mp_wasm_metal_io_ops).
 */
#ifndef MICROPY_INCLUDED_WASMMOD_PORTS_METAL_IO_OPS_H
#define MICROPY_INCLUDED_WASMMOD_PORTS_METAL_IO_OPS_H

#include "../../io.h"

#ifdef __cplusplus
extern "C" {
#endif

extern mp_wasm_io_ops_t mp_wasm_metal_io_ops;
void mp_wasm_metal_io_ops_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MICROPY_INCLUDED_WASMMOD_PORTS_METAL_IO_OPS_H */
