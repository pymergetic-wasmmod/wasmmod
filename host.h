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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_HOST_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "py/obj.h"

#ifndef MICROPY_PY_WASM_MATRIX
#define MICROPY_PY_WASM_MATRIX (0)
#endif

// Register WAMR natives for module "wasmmod.host".
bool mp_wasm_host_register(void);

// ---- Callable slots (guest → Python) ----

void mp_wasm_host_clear_all(void);
bool mp_wasm_host_set_slot(int32_t slot, mp_obj_t callable);
mp_obj_t mp_wasm_host_get_slot(int32_t slot);
size_t mp_wasm_host_slot_count(void);

// ---- Host mem cookies (opaque i32; durable host heap, Metal-style) ----
// 0 is never a valid cookie.

int32_t mp_wasm_mem_alloc(uint32_t size);
int32_t mp_wasm_mem_alloc_copy(const uint8_t *data, uint32_t len);
bool mp_wasm_mem_free(int32_t cookie);
void mp_wasm_mem_clear_all(void);
bool mp_wasm_mem_valid(int32_t cookie);
uint32_t mp_wasm_mem_len(int32_t cookie);
// Borrowed pointer valid until free/resize; NULL if bad cookie (or empty).
const uint8_t *mp_wasm_mem_data(int32_t cookie, uint32_t *len_out);
// Replace cookie contents (realloc).
bool mp_wasm_mem_set(int32_t cookie, const uint8_t *data, uint32_t len);

// ---- Python object handles (opaque i32; GC-rooted) ----
// 0 is never a valid handle.

int32_t mp_wasm_handle_register(mp_obj_t obj);
mp_obj_t mp_wasm_handle_resolve(int32_t handle); // mp_const_none if bad
bool mp_wasm_handle_free(int32_t handle);
void mp_wasm_handle_clear_all(void);

// ---- Host-language call helpers (C / RS on the host → guest) ----
// pack/func/mod/attr are UTF-8 (not necessarily NUL-terminated).
// Returns 0 on success, -1 on failure.

int mp_wasm_host_call_export_i32(const char *pack, size_t pack_len,
    const char *func, size_t func_len,
    uint32_t nargs, const int32_t *args, int32_t *out);

// Look up sys.modules[mod].attr and call. has_arg=0 → call0; else call1(int).
// On success *out is the Python result (any type).
int mp_wasm_host_call_attr(const char *mod, size_t mod_len,
    const char *attr, size_t attr_len,
    int has_arg, int32_t arg, mp_obj_t *out);

#if MICROPY_PY_WASM_MATRIX
// Call-matrix smoke callees (examples/wasmmod); not shipped in default builds.
MP_DECLARE_CONST_FUN_OBJ_1(mp_wasm_host_c_triple_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mp_wasm_host_rs_triple_obj);
#endif

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_HOST_H
