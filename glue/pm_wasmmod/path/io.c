/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_wasmmod/path/io.h"
#include "io.h"
#include <string.h>

/* Layout-compatible with mp_wasm_io_ops_t (version 2). */
void pm_wasmmod_io_set(const pm_wasmmod_io_ops_t *ops) {
    mp_wasm_io_set((const mp_wasm_io_ops_t *)(const void *)ops);
}
const pm_wasmmod_io_ops_t *pm_wasmmod_io_get(void) {
    return (const pm_wasmmod_io_ops_t *)(const void *)mp_wasm_io_get();
}
void pm_wasmmod_io_yield(void) {
    mp_wasm_io_yield();
}
