/*
 * This file is part of the MicroPython project, http://micropython.org/
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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_VERIFY_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_VERIFY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// MICROPY_WASM_VERIFY:
//   0 = off (default)
//   1 = require a valid signature for every load
//   2 = verify when .sig / wasmmod.sig present; otherwise allow
#ifndef MICROPY_WASM_VERIFY
#define MICROPY_WASM_VERIFY (0)
#endif

// Add a trusted public key (DER SubjectPublicKeyInfo or raw uncompressed P-256).
bool mp_wasm_trust_add(const uint8_t *key, size_t key_len);
void mp_wasm_trust_clear(void);
size_t mp_wasm_trust_count(void);

// Verify bytes before instantiate. path_hint may be NULL (bytes-only load).
// On failure fills errbuf and returns false.
bool mp_wasm_verify_bytes(const uint8_t *bytes, uint32_t len, const char *path_hint, char *errbuf, size_t errbuf_len);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_VERIFY_H
