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
#ifndef MP_WASM_ORIGIN_MAX
#define MP_WASM_ORIGIN_MAX (384)
#endif
#ifndef MP_WASM_ARCH_MAX
#define MP_WASM_ARCH_MAX (32)
#endif

// Opaque loaded-pack handle (Wasm/AOT via WAMR, or in-tree ELF + owned bytes).
// Note: C API mp_pack_load is distinct from the guest export string "mp_pack_load"
// (pack.toml lifecycle); same spelling, different worlds.
typedef struct mp_pack_t mp_pack_t;

bool mp_wasm_runtime_init(void);
void mp_wasm_runtime_deinit(void);

// Instantiate executed artifact bytes. Guest lifecycle export "mp_pack_load" is unrelated.
mp_pack_t *mp_pack_load(const uint8_t *bytes, uint32_t len, const char *name, char *errbuf, size_t errbuf_len);

mp_pack_t *mp_pack_load_ex(const uint8_t *code, uint32_t code_len,
    const uint8_t *meta, uint32_t meta_len, const char *name, const char *path_hint,
    char *errbuf, size_t errbuf_len);

void mp_pack_close(mp_pack_t *mod);

// Plain C unload-by-name — the native mirror of mp_pack_load, for callers
// (host or guest, any language) that need to unload without going through
// mp_obj_t/Python calling convention. sys.modules' Python wrapper is
// wasmmod.unload(); both end up here.
void mp_pack_unload_by_name(const char *name);

// Provenance (set at load from magic + path_hint).
const char *mp_pack_kind_str(const mp_pack_t *mod);   // "wasm" | "aot" | "elf" | ""
const char *mp_pack_origin(const mp_pack_t *mod);     // path/URL or ""
const char *mp_pack_arch(const mp_pack_t *mod);       // filename infix or ""
void mp_pack_parse_arch_from_path(const char *path, const char *pack_name, char *out, size_t out_len);

// True if kind is i32/i64/f32/f64.
bool mp_wasm_valkind_is_numeric(wasm_valkind_t kind);

// Look up export; fills param/result type arrays (caller frees with MICROPY_WASM_FREE / free).
// Returns false if missing or any type is non-numeric.
bool mp_pack_func_types(mp_pack_t *mod, const char *func,
    uint32_t *nparams_out, wasm_valkind_t **params_out,
    uint32_t *nresults_out, wasm_valkind_t **results_out);

// Typed call (heap argv; no fixed arity cap). results[] must have room for nresults.
bool mp_pack_call_vals(mp_pack_t *mod, const char *func,
    uint32_t nargs, wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results,
    char *errbuf, size_t errbuf_len);

// Hot path for guest→guest forwarders: lookup once, call many.
wasm_function_inst_t mp_pack_lookup_fn(mp_pack_t *mod, const char *func);
bool mp_pack_call_fn(mp_pack_t *mod, wasm_function_inst_t fn,
    uint32_t nargs, wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results,
    char *errbuf, size_t errbuf_len);

// Convenience: i32-only helpers (lifecycle, simple forwarders).
bool mp_pack_call0(mp_pack_t *mod, const char *func, int32_t *out_result, char *errbuf, size_t errbuf_len);
bool mp_pack_call_i32(mp_pack_t *mod, const char *func, const int32_t *args, uint32_t nargs, int32_t *out_result, char *errbuf, size_t errbuf_len);

uint32_t mp_pack_export_names(mp_pack_t *mod, const char **names, uint32_t max_names);

const char *mp_pack_name(const mp_pack_t *mod);
void mp_pack_set_name(mp_pack_t *mod, const char *name);

const uint8_t *mp_pack_bytes(const mp_pack_t *mod, uint32_t *len_out);
const uint8_t *mp_pack_meta_bytes(const mp_pack_t *mod, uint32_t *len_out);

// Visit exports whose params/results are all numeric (any arity / result count).
typedef void (*mp_wasm_numeric_export_cb)(const char *name, uint32_t nparams, uint32_t nresults, void *ctx);
void mp_pack_foreach_numeric_export(mp_pack_t *mod, mp_wasm_numeric_export_cb cb, void *ctx);

#if MICROPY_PY_WASM_ELF
// Drop ELF→Wasm PLT slots that target this module (on unload / replace).
void mp_pack_elf_plt_invalidate(mp_pack_t *mod);
#endif

// True if export exists and is numeric; optionally returns arity.
bool mp_pack_numeric_export_arity(mp_pack_t *mod, const char *name,
    uint32_t *nparams_out, uint32_t *nresults_out);

// Back-compat aliases (i32-only filter).
typedef mp_wasm_numeric_export_cb mp_wasm_i32_export_cb;
void mp_pack_foreach_i32_export(mp_pack_t *mod, mp_wasm_i32_export_cb cb, void *ctx);
bool mp_pack_i32_export_arity(mp_pack_t *mod, const char *name, uint32_t *nparams_out);

// Linear memory: guest i32 offsets → host pointer (validated).
wasm_module_inst_t mp_pack_inst(mp_pack_t *mod);
bool mp_wasm_linear_from_exec(wasm_exec_env_t exec_env, uint32_t off, uint32_t n, void **out);
bool mp_pack_linear(mp_pack_t *mod, uint32_t off, uint32_t n, void **out);
bool mp_pack_mem_read(mp_pack_t *mod, uint32_t off, uint32_t n, void *dst);
bool mp_pack_mem_write(mp_pack_t *mod, uint32_t off, uint32_t n, const void *src);
// Allocate in guest linear heap; returns app offset (0 = fail).
uint32_t mp_pack_mem_alloc(mp_pack_t *mod, uint32_t n, void **native_out);
void mp_pack_mem_free(mp_pack_t *mod, uint32_t off);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_RUNTIME_H
