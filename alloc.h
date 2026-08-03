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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_ALLOC_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_ALLOC_H

#include <stddef.h>

// Port override: define MICROPY_WASM_MALLOC / FREE / REALLOC before including.
#ifndef MICROPY_WASM_MALLOC
#include <stdlib.h>
static inline void *mp_wasm_stdlib_malloc(size_t n) {
    return malloc(n);
}
static inline void mp_wasm_stdlib_free(void *p) {
    free(p);
}
static inline void *mp_wasm_stdlib_realloc(void *p, size_t n) {
    return realloc(p, n);
}
#define MICROPY_WASM_MALLOC(n) mp_wasm_stdlib_malloc(n)
#define MICROPY_WASM_FREE(p) mp_wasm_stdlib_free(p)
#define MICROPY_WASM_REALLOC(p, n) mp_wasm_stdlib_realloc((p), (n))
#else
#ifndef MICROPY_WASM_FREE
#error "MICROPY_WASM_MALLOC requires MICROPY_WASM_FREE"
#endif
#ifndef MICROPY_WASM_REALLOC
// Optional; only some translation units need it.
#endif
#endif

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_ALLOC_H
