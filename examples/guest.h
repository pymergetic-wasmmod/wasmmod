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

/*
 * Shared helpers for freestanding Wasm guest sources.
 *
 * import_module / import_name are Clang Wasm attributes. Host clangd (and
 * non-Wasm compilers) do not know them — gate so IDE analysis stays clean.
 */
#ifndef MICROPY_WASMMOD_GUEST_H
#define MICROPY_WASMMOD_GUEST_H

#include <stdint.h>

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
#define MP_WASM_IMPORT_ATTR(module, name) \
    __attribute__((import_module(module), import_name(name)))
#else
#define MP_WASM_IMPORT_ATTR(module, name)
#endif

/**
 * Declare a Wasm import. `fn` is both the C symbol and the import field name.
 *
 *   MP_WASM_IMPORT("hello", int, hello, void);
 *   MP_WASM_IMPORT("micropython.host", int, call_i32, int slot, int arg);
 */
#define MP_WASM_IMPORT(module, ret, fn, ...) \
    MP_WASM_IMPORT_ATTR(module, #fn) ret fn(__VA_ARGS__)

/**
 * Like MP_WASM_IMPORT, but C symbol differs from the Wasm import field name.
 *
 *   MP_WASM_IMPORT_AS("micropython.host", "call_i32", int, host_call, int, int);
 */
#define MP_WASM_IMPORT_AS(module, wasm_name, ret, c_name, ...) \
    MP_WASM_IMPORT_ATTR(module, wasm_name) ret c_name(__VA_ARGS__)

/**
 * Guest pointer → i32 linear offset for Wasm ABI (host resolves via
 * validate_app_addr / addr_app_to_native, or mem_copy_* cookies).
 */
#define MP_WASM_PTR(p) ((int32_t)(uintptr_t)(p))

#endif /* MICROPY_WASMMOD_GUEST_H */
