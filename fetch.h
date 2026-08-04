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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_FETCH_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_FETCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "py/misc.h"

// Fetch URI into *out (vstr). Order: port I/O ops → legacy MICROPY_WASM_FETCH
// → native HTTP (if http(s)) → VFS. See io.h / ports/PORT.md for Metal.
bool mp_wasm_fetch(const char *uri, vstr_t *out, char *errbuf, size_t errbuf_len);

// True if http(s) URI responds with 200. Uses ops->probe (or fetch), else native.
bool mp_wasm_http_probe(const char *uri);

bool mp_wasm_uri_is_http(const char *uri);

// Join root + relative path (root may be a directory or URL prefix).
void mp_wasm_join_uri(const char *root, const char *rel, vstr_t *out);

// Optional Bearer token for native HTTP (CDN). Pass NULL to clear.
void mp_wasm_fetch_set_auth_bearer(const char *token);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_FETCH_H
