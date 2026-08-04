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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_IO_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_IO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Replaceable host I/O (HTTP / custom URI). Finder + load always call
// mp_wasm_fetch / mp_wasm_http_probe — never the POSIX client directly.
//
// Metal: set ops (compile-time MICROPY_WASM_IO_OPS or runtime mp_wasm_io_set)
// so fetch/probe run your async stack to completion (or park the caller).
// Sync signatures stay; async callbacks can land in reserved slots later
// without changing wasmmod call sites. See ports/PORT.md.
// ---------------------------------------------------------------------------

#define MP_WASM_IO_OPS_VERSION (2u)

typedef enum {
    MP_WASM_IO_OK = 0,       // success (fetch: bytes valid; probe: exists)
    MP_WASM_IO_DECLINE = 1,  // not handled — try default backend
    MP_WASM_IO_ERR = -1,     // handled but failed (404 / network / …)
} mp_wasm_io_result_t;

// Future async (Metal): non-NULL when ops.version grows; unused at v1.
typedef void (*mp_wasm_io_fetch_cb_t)(void *ctx, mp_wasm_io_result_t st,
    uint8_t *buf, uint32_t len);
typedef void (*mp_wasm_io_probe_cb_t)(void *ctx, mp_wasm_io_result_t st);

// Optional write/publish hook (v2). method e.g. "POST"; body may be NULL.
// DECLINE / NULL → publish not supported on this host.
typedef mp_wasm_io_result_t (*mp_wasm_io_request_t)(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len,
    const char *content_type,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len);

typedef struct mp_wasm_io_ops_t {
    uint32_t version; // MP_WASM_IO_OPS_VERSION

    // GET (or equivalent) uri → *out_bytes / *out_len (MICROPY_WASM_MALLOC).
    // Caller frees with MICROPY_WASM_FREE on OK. DECLINE → default VFS/HTTP.
    mp_wasm_io_result_t (*fetch)(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
        char *errbuf, size_t errbuf_len);

    // Existence check (HTTP 200). NULL → synthesize via fetch + discard.
    // DECLINE → default probe / native HEAD.
    mp_wasm_io_result_t (*probe)(const char *uri);

    // Optional cooperative yield during long default I/O (Metal scheduler).
    void (*yield)(void);

    // v2 write path (browser/native multipart publish). NULL until wired.
    mp_wasm_io_request_t request;

    // Reserved put alias / future slot (must be NULL if unused).
    void *put;

    void *userdata;
} mp_wasm_io_ops_t;

// Install port ops (NULL restores built-in defaults). Not copied — pointer
// must remain valid. Safe to call before any fetch.
void mp_wasm_io_set(const mp_wasm_io_ops_t *ops);
const mp_wasm_io_ops_t *mp_wasm_io_get(void);

// Invoke ops->yield if present (used by default HTTP loops).
void mp_wasm_io_yield(void);

// ---------------------------------------------------------------------------
// Other port hooks (compile-time macros — see ports/PORT.md)
//
//   MICROPY_WASM_MALLOC / REALLOC / FREE
//   MICROPY_WASM_IO_OPS          — static mp_wasm_io_ops_t used as default
//   MICROPY_WASM_FETCH(...)      — legacy shim → DECLINE chain (prefer IO_OPS)
//   MICROPY_WASM_VERIFY_HOOK(...)
//   MICROPY_WASM_EXPORT_PUBLISH(module, export, fn_ptr)
//   MICROPY_WASM_STACK_SIZE / HEAP_SIZE
// ---------------------------------------------------------------------------

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_IO_H
