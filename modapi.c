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


#include "py/mperrno.h"
#include "py/runtime.h"

#if MICROPY_PY_WASM

#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/mod.h"
#include "extmod/wasmmod/verify.h"
// wasm.verify() → bool; wasm.verify(False) → session gate for all loads (VFS+HTTP).
static mp_obj_t mod_wasm_verify(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return mp_obj_new_bool(mp_wasm_get_verify_enabled());
    }
    mp_wasm_set_verify_enabled(mp_obj_is_true(args[0]));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_verify_obj, 0, 1, mod_wasm_verify);

static mp_obj_t mod_wasm_add_trust(mp_obj_t key_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(key_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_wasm_trust_add(bufinfo.buf, bufinfo.len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.add_trust: bad key or trust full"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_add_trust_obj, mod_wasm_add_trust);

static mp_obj_t mod_wasm_trust_clear(void) {
    mp_wasm_trust_clear();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_trust_clear_obj, mod_wasm_trust_clear);

static mp_obj_t mod_wasm_trust_count(void) {
    return MP_OBJ_NEW_SMALL_INT(mp_wasm_trust_count());
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_trust_count_obj, mod_wasm_trust_count);

static mp_obj_t mod_wasm_host_set(mp_obj_t slot_in, mp_obj_t callable) {
    int32_t slot = (int32_t)mp_obj_get_int(slot_in);
    if (!mp_wasm_host_set_slot(slot, callable)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.host_set: bad slot or callable"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_host_set_obj, mod_wasm_host_set);

static mp_obj_t mod_wasm_host_get(mp_obj_t slot_in) {
    return mp_wasm_host_get_slot((int32_t)mp_obj_get_int(slot_in));
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_host_get_obj, mod_wasm_host_get);

static mp_obj_t mod_wasm_host_clear(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        mp_wasm_host_clear_all();
        return mp_const_none;
    }
    if (!mp_wasm_host_set_slot((int32_t)mp_obj_get_int(args[0]), mp_const_none)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.host_clear: bad slot"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_host_clear_obj, 0, 1, mod_wasm_host_clear);

// mem_alloc(n: int) or mem_alloc(data: bytes-like)
static mp_obj_t mod_wasm_mem_alloc(mp_obj_t arg) {
    if (mp_obj_is_int(arg)) {
        mp_int_t n = mp_obj_get_int(arg);
        if (n < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("mem_alloc: negative size"));
        }
        int32_t c = mp_wasm_mem_alloc((uint32_t)n);
        if (c == 0 && n != 0) {
            mp_raise_OSError(MP_ENOMEM);
        }
        return mp_obj_new_int(c);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(arg, &bufinfo, MP_BUFFER_READ);
    int32_t c = mp_wasm_mem_alloc_copy(bufinfo.buf, (uint32_t)bufinfo.len);
    if (c == 0 && bufinfo.len != 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_obj_new_int(c);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_alloc_obj, mod_wasm_mem_alloc);

static mp_obj_t mod_wasm_mem_free(mp_obj_t cookie_in) {
    if (!mp_wasm_mem_free((int32_t)mp_obj_get_int(cookie_in))) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_free: bad cookie"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_free_obj, mod_wasm_mem_free);

static mp_obj_t mod_wasm_mem_get(mp_obj_t cookie_in) {
    int32_t cookie = (int32_t)mp_obj_get_int(cookie_in);
    if (!mp_wasm_mem_valid(cookie)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_get: bad cookie"));
    }
    uint32_t n = 0;
    const uint8_t *p = mp_wasm_mem_data(cookie, &n);
    return mp_obj_new_bytes(p, (size_t)n);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_get_obj, mod_wasm_mem_get);

static mp_obj_t mod_wasm_mem_set(mp_obj_t cookie_in, mp_obj_t data_in) {
    int32_t cookie = (int32_t)mp_obj_get_int(cookie_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_wasm_mem_set(cookie, bufinfo.buf, (uint32_t)bufinfo.len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_set: bad cookie or OOM"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_mem_set_obj, mod_wasm_mem_set);

static mp_obj_t mod_wasm_mem_clear(void) {
    mp_wasm_mem_clear_all();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_mem_clear_obj, mod_wasm_mem_clear);

static mp_obj_t mod_wasm_handle_register(mp_obj_t obj) {
    int32_t h = mp_wasm_handle_register(obj);
    if (h <= 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_obj_new_int(h);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_register_obj, mod_wasm_handle_register);

static mp_obj_t mod_wasm_handle_resolve(mp_obj_t handle_in) {
    return mp_wasm_handle_resolve((int32_t)mp_obj_get_int(handle_in));
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_resolve_obj, mod_wasm_handle_resolve);

static mp_obj_t mod_wasm_handle_free(mp_obj_t handle_in) {
    if (!mp_wasm_handle_free((int32_t)mp_obj_get_int(handle_in))) {
        mp_raise_ValueError(MP_ERROR_TEXT("handle_free: bad handle"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_free_obj, mod_wasm_handle_free);

static mp_obj_t mod_wasm_handle_clear(void) {
    mp_wasm_handle_clear_all();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_handle_clear_obj, mod_wasm_handle_clear);

#if MICROPY_PY_WASM_MATRIX
static void require_str(mp_obj_t o, const char **data, size_t *len) {
    *data = mp_obj_str_get_data(o, len);
}

// Matrix-only: host C → guest Wasm export (i32).
static mp_obj_t mod_wasm_c_call(size_t n_args, const mp_obj_t *args) {
    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("c_call needs pack, func"));
    }
    const char *pack;
    size_t pack_len;
    const char *func;
    size_t func_len;
    require_str(args[0], &pack, &pack_len);
    require_str(args[1], &func, &func_len);
    uint32_t nargs = (uint32_t)(n_args - 2);
    int32_t stack_args[4];
    int32_t *iargs = NULL;
    if (nargs > 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("c_call: too many args"));
    }
    if (nargs > 0) {
        for (uint32_t i = 0; i < nargs; ++i) {
            stack_args[i] = (int32_t)mp_obj_get_int(args[2 + i]);
        }
        iargs = stack_args;
    }
    int32_t out = 0;
    if (mp_wasm_host_call_export_i32(pack, pack_len, func, func_len, nargs, iargs, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("c_call: export call failed"));
    }
    return mp_obj_new_int(out);
}
MP_DEFINE_CONST_FUN_OBJ_VAR(mod_wasm_c_call_obj, 2, mod_wasm_c_call);

static mp_obj_t mod_wasm_c_call_attr(size_t n_args, const mp_obj_t *args) {
    const char *mod;
    size_t mod_len;
    const char *attr;
    size_t attr_len;
    require_str(args[0], &mod, &mod_len);
    require_str(args[1], &attr, &attr_len);
    int has_arg = n_args > 2;
    int32_t arg = has_arg ? (int32_t)mp_obj_get_int(args[2]) : 0;
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_call_attr(mod, mod_len, attr, attr_len, has_arg, arg, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("c_call_attr: call failed"));
    }
    return out;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_c_call_attr_obj, 2, 3, mod_wasm_c_call_attr);

// examples/wasmmod/host_matrix.rs
int mp_wasm_host_rs_call_export_i32(const char *pack, size_t pack_len,
    const char *func, size_t func_len, uint32_t nargs, const int32_t *args, int32_t *out);
int mp_wasm_host_rs_call_attr(const char *mod, size_t mod_len,
    const char *attr, size_t attr_len, int has_arg, int32_t arg, mp_obj_t *out);

static mp_obj_t mod_wasm_rs_call(size_t n_args, const mp_obj_t *args) {
    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("rs_call needs pack, func"));
    }
    const char *pack;
    size_t pack_len;
    const char *func;
    size_t func_len;
    require_str(args[0], &pack, &pack_len);
    require_str(args[1], &func, &func_len);
    uint32_t nargs = (uint32_t)(n_args - 2);
    int32_t stack_args[4];
    int32_t *iargs = NULL;
    if (nargs > 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("rs_call: too many args"));
    }
    if (nargs > 0) {
        for (uint32_t i = 0; i < nargs; ++i) {
            stack_args[i] = (int32_t)mp_obj_get_int(args[2 + i]);
        }
        iargs = stack_args;
    }
    int32_t out = 0;
    if (mp_wasm_host_rs_call_export_i32(pack, pack_len, func, func_len, nargs, iargs, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("rs_call: export call failed"));
    }
    return mp_obj_new_int(out);
}
MP_DEFINE_CONST_FUN_OBJ_VAR(mod_wasm_rs_call_obj, 2, mod_wasm_rs_call);

static mp_obj_t mod_wasm_rs_call_attr(size_t n_args, const mp_obj_t *args) {
    const char *mod;
    size_t mod_len;
    const char *attr;
    size_t attr_len;
    require_str(args[0], &mod, &mod_len);
    require_str(args[1], &attr, &attr_len);
    int has_arg = n_args > 2;
    int32_t arg = has_arg ? (int32_t)mp_obj_get_int(args[2]) : 0;
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_rs_call_attr(mod, mod_len, attr, attr_len, has_arg, arg, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("rs_call_attr: call failed"));
    }
    return out;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_rs_call_attr_obj, 2, 3, mod_wasm_rs_call_attr);
#endif // MICROPY_PY_WASM_MATRIX

#endif // MICROPY_PY_WASM
