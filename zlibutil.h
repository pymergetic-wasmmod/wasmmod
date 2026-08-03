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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_ZLIBUTIL_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_ZLIBUTIL_H

#include <stdbool.h>
#include <stdint.h>

// Whole-artifact zlib envelope: MPZL | u32le raw_len | zlib(bytes).
#define MP_WASM_ARTIFACT_ZLIB_MAGIC "MPZL"

// Inflate a zlib (RFC 1950) blob into dst (exactly dst_len bytes expected).
bool mp_wasm_zlib_inflate(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_len);

// If buf starts with MPZL envelope, replace *inout / *inout_len with inflated
// bytes (malloc). Returns false on corrupt envelope. No-op (true) if not MPZL.
// *owned is set when a new buffer was allocated (caller frees on replace).
bool mp_wasm_artifact_unwrap_zlib(const uint8_t **inout, uint32_t *inout_len, uint8_t **owned);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_ZLIBUTIL_H
