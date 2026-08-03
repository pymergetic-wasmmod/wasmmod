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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_SOURCE_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MP_WASM_SOURCE_SECTION "wasmmod.source"
#define MP_WASM_SOURCE_MAGIC "MPSR"
#define MP_WASM_SOURCE_VERSION 1

#define MP_WASM_SOURCE_FILE_FLAG_ZLIB (1u << 0)

typedef struct mp_wasm_source_file_t {
    const char *path;
    uint16_t path_len;
    uint8_t flags;
    uint32_t raw_len;
    const uint8_t *data;
    uint32_t data_len;
} mp_wasm_source_file_t;

typedef struct mp_wasm_source_tag_t {
    const char *key;
    uint16_t key_len;
    const char *value;
    uint16_t value_len;
} mp_wasm_source_tag_t;

typedef struct mp_wasm_source_info_t {
    uint16_t version;
    uint16_t flags;
    const char *name;
    uint16_t name_len;
    const char *pkg_version;
    uint16_t pkg_version_len;
    const mp_wasm_source_tag_t *tags;
    uint16_t n_tags;
    const mp_wasm_source_file_t *files;
    uint32_t n_files;
} mp_wasm_source_info_t;

// Opaque view: parsed table + optional owned wasm buffer.
typedef struct mp_wasm_source_view_t mp_wasm_source_view_t;

bool mp_wasm_source_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len);
bool mp_wasm_source_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_source_info_t *out);
void mp_wasm_source_info_free(mp_wasm_source_info_t *info);

// Open: buffer borrows wasm; file owns a copy; name resolves via loaded pack (__wasm__).
mp_wasm_source_view_t *mp_wasm_source_open_buffer(const uint8_t *wasm, uint32_t len);
// Takes ownership of wasm (freed on close).
mp_wasm_source_view_t *mp_wasm_source_open_owned(uint8_t *wasm, uint32_t len);
mp_wasm_source_view_t *mp_wasm_source_open_file(const char *path);
mp_wasm_source_view_t *mp_wasm_source_open_name(const char *pack_name);
void mp_wasm_source_close(mp_wasm_source_view_t *v);

const mp_wasm_source_info_t *mp_wasm_source_info(const mp_wasm_source_view_t *v);

// Read file (decompress if needed). Caller frees *out with MICROPY_WASM_FREE.
bool mp_wasm_source_read(const mp_wasm_source_view_t *v, const char *path, uint8_t **out, uint32_t *out_len);

// Module mount prefix detection (e.g. "src/"); writes into buf, returns length, 0 if none.
size_t mp_wasm_source_mount_prefix(const mp_wasm_source_view_t *v, char *buf, size_t buf_len);

// Callback iterators (return non-zero to stop).
typedef int (*mp_wasm_source_path_cb)(void *ctx, const char *path, size_t path_len);
typedef int (*mp_wasm_source_name_cb)(void *ctx, const char *name, size_t name_len);

// List all stored paths, or those under module (relative dotted, "" = pack root).
int mp_wasm_source_list_files(const mp_wasm_source_view_t *v, const char *module_or_null,
    void *ctx, mp_wasm_source_path_cb cb);

// Dotted module names relative to pack ("" omitted; root implied by pack name).
int mp_wasm_source_list_modules(const mp_wasm_source_view_t *v, void *ctx, mp_wasm_source_name_cb cb);

// Immediate child module names under parent ("" = top-level under mount).
int mp_wasm_source_list_submodules(const mp_wasm_source_view_t *v, const char *parent,
    void *ctx, mp_wasm_source_name_cb cb);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_SOURCE_H
