/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_RUNTIME_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "wasm_export.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM_ELF
#include "extmod/wasmmod/format/elf/load.h"
#endif

// Match MicroPython qstr length (see qstr.h); +1 for NUL.
#ifndef MP_WASM_NAME_MAX
#define MP_WASM_NAME_MAX (255)
#endif

// Opaque loaded-module handle (WAMR instance + exec env + owned bytes).
typedef struct mp_wasm_module_t mp_wasm_module_t;

bool mp_wasm_runtime_init(void);
void mp_wasm_runtime_deinit(void);

mp_wasm_module_t *mp_wasm_module_load(const uint8_t *bytes, uint32_t len, const char *name, char *errbuf, size_t errbuf_len);

mp_wasm_module_t *mp_wasm_module_load_ex(const uint8_t *code, uint32_t code_len,
    const uint8_t *meta, uint32_t meta_len, const char *name, const char *path_hint,
    char *errbuf, size_t errbuf_len);

void mp_wasm_module_close(mp_wasm_module_t *mod);

// True if kind is i32/i64/f32/f64.
bool mp_wasm_valkind_is_numeric(wasm_valkind_t kind);

// Look up export; fills param/result type arrays (caller frees with MICROPY_WASM_FREE / free).
// Returns false if missing or any type is non-numeric.
bool mp_wasm_module_func_types(mp_wasm_module_t *mod, const char *func,
    uint32_t *nparams_out, wasm_valkind_t **params_out,
    uint32_t *nresults_out, wasm_valkind_t **results_out);

// Typed call (heap argv; no fixed arity cap). results[] must have room for nresults.
bool mp_wasm_module_call_vals(mp_wasm_module_t *mod, const char *func,
    uint32_t nargs, wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results,
    char *errbuf, size_t errbuf_len);

// Hot path for guest→guest forwarders: lookup once, call many.
wasm_function_inst_t mp_wasm_module_lookup_fn(mp_wasm_module_t *mod, const char *func);
bool mp_wasm_module_call_fn(mp_wasm_module_t *mod, wasm_function_inst_t fn,
    uint32_t nargs, wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results,
    char *errbuf, size_t errbuf_len);

// Convenience: i32-only helpers (lifecycle, simple forwarders).
bool mp_wasm_module_call0(mp_wasm_module_t *mod, const char *func, int32_t *out_result, char *errbuf, size_t errbuf_len);
bool mp_wasm_module_call_i32(mp_wasm_module_t *mod, const char *func, const int32_t *args, uint32_t nargs, int32_t *out_result, char *errbuf, size_t errbuf_len);

uint32_t mp_wasm_module_export_names(mp_wasm_module_t *mod, const char **names, uint32_t max_names);

const char *mp_wasm_module_name(const mp_wasm_module_t *mod);
void mp_wasm_module_set_name(mp_wasm_module_t *mod, const char *name);

const uint8_t *mp_wasm_module_bytes(const mp_wasm_module_t *mod, uint32_t *len_out);
const uint8_t *mp_wasm_module_meta_bytes(const mp_wasm_module_t *mod, uint32_t *len_out);

// Visit exports whose params/results are all numeric (any arity / result count).
typedef void (*mp_wasm_numeric_export_cb)(const char *name, uint32_t nparams, uint32_t nresults, void *ctx);
void mp_wasm_module_foreach_numeric_export(mp_wasm_module_t *mod, mp_wasm_numeric_export_cb cb, void *ctx);

#if MICROPY_PY_WASM_ELF
// Drop ELF→Wasm PLT slots that target this module (on unload / replace).
void mp_wasm_elf_plt_invalidate(mp_wasm_module_t *mod);
#endif

// True if export exists and is numeric; optionally returns arity.
bool mp_wasm_module_numeric_export_arity(mp_wasm_module_t *mod, const char *name,
    uint32_t *nparams_out, uint32_t *nresults_out);

// Back-compat aliases (i32-only filter).
typedef mp_wasm_numeric_export_cb mp_wasm_i32_export_cb;
void mp_wasm_module_foreach_i32_export(mp_wasm_module_t *mod, mp_wasm_i32_export_cb cb, void *ctx);
bool mp_wasm_module_i32_export_arity(mp_wasm_module_t *mod, const char *name, uint32_t *nparams_out);

// Linear memory: guest i32 offsets → host pointer (validated).
wasm_module_inst_t mp_wasm_module_inst(mp_wasm_module_t *mod);
bool mp_wasm_linear_from_exec(wasm_exec_env_t exec_env, uint32_t off, uint32_t n, void **out);
bool mp_wasm_module_linear(mp_wasm_module_t *mod, uint32_t off, uint32_t n, void **out);
bool mp_wasm_module_mem_read(mp_wasm_module_t *mod, uint32_t off, uint32_t n, void *dst);
bool mp_wasm_module_mem_write(mp_wasm_module_t *mod, uint32_t off, uint32_t n, const void *src);
// Allocate in guest linear heap; returns app offset (0 = fail).
uint32_t mp_wasm_module_mem_alloc(mp_wasm_module_t *mod, uint32_t n, void **native_out);
void mp_wasm_module_mem_free(mp_wasm_module_t *mod, uint32_t off);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_RUNTIME_H
