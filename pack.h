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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_PACK_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_PACK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Project-scoped custom section / import-module names (host-agnostic). */
#define MP_WASM_PACK_SECTION "wasmmod.pack"
#define MP_WASM_IMPORTS_SECTION "wasmmod.imports"
#define MP_WASM_DEPS_SECTION "wasmmod.deps"
#define MP_WASM_MODULE "wasmmod"           /* guest loader API (version, call_i32, …) */
#define MP_WASM_HOST_MODULE "wasmmod.host" /* guest→host slots / mem / call_py */
#define MP_WASM_SIG_SECTION "wasmmod.sig"
#define MP_WASM_PACK_MAGIC "MPWP"
#define MP_WASM_IMPORTS_MAGIC "MPWI"
#define MP_WASM_DEPS_MAGIC "MPWD"
#define MP_WASM_PACK_KIND_PY 1
#define MP_WASM_PACK_KIND_MPY 2
#define MP_WASM_PACK_KIND_RAW 3
#define MP_WASM_PACK_KIND_PYC 4 /* CPython bytecode; ignored by MicroPython loader */

// Pack file entry flags (version >= 3).
#define MP_WASM_PACK_FILE_FLAG_ZLIB (1u << 0)

// Signature tags (see docs/PACK.md). 0..8 = N i32 args -> i32.
// 255 = resolve arity from the Wasm type at bind time.
#define MP_WASM_PACK_SIG_AUTO 255

// File entry: path/data point into the original wasm buffer (no copy).
typedef struct mp_wasm_pack_file_t {
    const char *path;
    uint16_t path_len;
    uint8_t kind;
    uint8_t flags;     // v3+; 0 for v1/v2
    uint32_t raw_len;  // v3+ uncompressed size; == data_len when not zlib
    const uint8_t *data;
    uint32_t data_len;
} mp_wasm_pack_file_t;

typedef struct mp_wasm_pack_export_t {
    const char *module; // relative dotted suffix; empty = pack root
    uint16_t module_len;
    const char *func;
    uint16_t func_len;
    const char *export_name;
    uint16_t export_len;
    uint8_t sig;
} mp_wasm_pack_export_t;

typedef struct mp_wasm_pack_info_t {
    const char *name;
    uint16_t name_len;
    uint16_t version;
    uint16_t flags;
    const mp_wasm_pack_file_t *files;
    uint32_t n_files;
    const mp_wasm_pack_export_t *exports; // v2+; may be NULL
    uint32_t n_exports;
} mp_wasm_pack_info_t;

typedef struct mp_wasm_import_t {
    const char *module;
    uint16_t module_len;
    const char *func;
    uint16_t func_len;
} mp_wasm_import_t;

typedef struct mp_wasm_imports_info_t {
    uint16_t version;
    const mp_wasm_import_t *imports;
    uint32_t n_imports;
} mp_wasm_imports_info_t;

typedef struct mp_wasm_dep_t {
    const char *name;
    uint16_t name_len;
    const char *version;
    uint16_t version_len;
} mp_wasm_dep_t;

typedef struct mp_wasm_deps_info_t {
    uint16_t version;
    const mp_wasm_dep_t *deps;
    uint32_t n_deps;
} mp_wasm_deps_info_t;

// Low-level Wasm helpers (used by pack + forwarder).
bool mp_wasm_read_uleb(const uint8_t **p, const uint8_t *end, uint32_t *out);
bool mp_wasm_find_section_id(const uint8_t *wasm, uint32_t len, uint8_t id, const uint8_t **payload, uint32_t *payload_len);
bool mp_wasm_find_custom_section(const uint8_t *wasm, uint32_t len, const char *name, const uint8_t **payload, uint32_t *payload_len);

bool mp_wasm_pack_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len);
bool mp_wasm_pack_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_pack_info_t *out);
void mp_wasm_pack_info_free(mp_wasm_pack_info_t *info);
// Inflate pack file if needed. *out points at f->data or malloc'd buffer (*to_free).
// Caller MICROPY_WASM_FREE(*to_free) when non-NULL.
bool mp_wasm_pack_file_bytes(const mp_wasm_pack_file_t *f, const uint8_t **out, uint32_t *out_len, uint8_t **to_free);

bool mp_wasm_imports_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len);
bool mp_wasm_imports_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_imports_info_t *out);
void mp_wasm_imports_info_free(mp_wasm_imports_info_t *info);

bool mp_wasm_deps_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len);
bool mp_wasm_deps_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_deps_info_t *out);
void mp_wasm_deps_info_free(mp_wasm_deps_info_t *info);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_PACK_H
