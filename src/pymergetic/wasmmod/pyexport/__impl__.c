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
 *
 * See __exports__.h. Each pool is a fixed array of {used} slots (callables are
 * GC-rooted) plus that many distinct C functions — a slot's identity (function
 * address) is what native callers resolve, so concurrent exports of the same
 * arity need distinct compiled functions.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "pymergetic/wasmmod/pyexport/__exports__.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

#include "ports/common/memcookie.h"
#include "ports/micropython/objhandle.h"

typedef struct {
    bool used;
} pm_pyexport_slot_t;

/* ---- arity: 0 ---- */
#define PM_PYEXPORT_V_POOL 8
static pm_pyexport_slot_t g_pyexport_v[PM_PYEXPORT_V_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_v_callables[PM_PYEXPORT_V_POOL]);
#define PM_PYEXPORT_DEF_V(i) \
    static int32_t pm_wasmmod_pyexport_v_##i(void) { \
        if (!g_pyexport_v[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_v_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t res = mp_call_function_n_kw(cb, 0, 0, NULL); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return 0; \
    }
#define PM_PYEXPORT_V_IDX(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7)
PM_PYEXPORT_V_IDX(PM_PYEXPORT_DEF_V)
#define PM_PYEXPORT_V_PTR(i) (void *)pm_wasmmod_pyexport_v_##i,
static void *const g_pyexport_v_ptrs[PM_PYEXPORT_V_POOL] = {
    PM_PYEXPORT_V_IDX(PM_PYEXPORT_V_PTR)
};

/* ---- arity: 1 ---- */
#define PM_PYEXPORT_1_POOL 8
static pm_pyexport_slot_t g_pyexport_1[PM_PYEXPORT_1_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_1_callables[PM_PYEXPORT_1_POOL]);
#define PM_PYEXPORT_DEF_1(i) \
    static int32_t pm_wasmmod_pyexport_1_##i(int32_t a0) { \
        if (!g_pyexport_1[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_1_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[1] = { mp_obj_new_int(a0) }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 1, 0, args); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return 0; \
    }
#define PM_PYEXPORT_1_IDX(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7)
PM_PYEXPORT_1_IDX(PM_PYEXPORT_DEF_1)
#define PM_PYEXPORT_1_PTR(i) (void *)pm_wasmmod_pyexport_1_##i,
static void *const g_pyexport_1_ptrs[PM_PYEXPORT_1_POOL] = {
    PM_PYEXPORT_1_IDX(PM_PYEXPORT_1_PTR)
};

/* ---- arity: 2 ---- */
#define PM_PYEXPORT_2_POOL 4
static pm_pyexport_slot_t g_pyexport_2[PM_PYEXPORT_2_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_2_callables[PM_PYEXPORT_2_POOL]);
#define PM_PYEXPORT_DEF_2(i) \
    static int32_t pm_wasmmod_pyexport_2_##i(int32_t a0, int32_t a1) { \
        if (!g_pyexport_2[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_2_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[2] = { mp_obj_new_int(a0), mp_obj_new_int(a1) }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 2, 0, args); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return 0; \
    }
#define PM_PYEXPORT_2_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_2_IDX(PM_PYEXPORT_DEF_2)
#define PM_PYEXPORT_2_PTR(i) (void *)pm_wasmmod_pyexport_2_##i,
static void *const g_pyexport_2_ptrs[PM_PYEXPORT_2_POOL] = {
    PM_PYEXPORT_2_IDX(PM_PYEXPORT_2_PTR)
};

/* ---- arity: 3 ---- */
#define PM_PYEXPORT_3_POOL 4
static pm_pyexport_slot_t g_pyexport_3[PM_PYEXPORT_3_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_3_callables[PM_PYEXPORT_3_POOL]);
#define PM_PYEXPORT_DEF_3(i) \
    static int32_t pm_wasmmod_pyexport_3_##i(int32_t a0, int32_t a1, int32_t a2) { \
        if (!g_pyexport_3[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_3_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[3] = { mp_obj_new_int(a0), mp_obj_new_int(a1), mp_obj_new_int(a2) }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 3, 0, args); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return 0; \
    }
#define PM_PYEXPORT_3_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_3_IDX(PM_PYEXPORT_DEF_3)
#define PM_PYEXPORT_3_PTR(i) (void *)pm_wasmmod_pyexport_3_##i,
static void *const g_pyexport_3_ptrs[PM_PYEXPORT_3_POOL] = {
    PM_PYEXPORT_3_IDX(PM_PYEXPORT_3_PTR)
};

/* ---- i64 (1 arg) ---- */
#define PM_PYEXPORT_I64_POOL 4
static pm_pyexport_slot_t g_pyexport_i64[PM_PYEXPORT_I64_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_i64_callables[PM_PYEXPORT_I64_POOL]);
#define PM_PYEXPORT_DEF_I64(i) \
    static int64_t pm_wasmmod_pyexport_i64_##i(int64_t a0) { \
        if (!g_pyexport_i64[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_i64_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[1] = { mp_obj_new_int_from_ll(a0) }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 1, 0, args); \
            int64_t out = (int64_t)mp_obj_get_ll(res); \
            nlr_pop(); \
            return out; \
        } \
        return 0; \
    }
#define PM_PYEXPORT_I64_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_I64_IDX(PM_PYEXPORT_DEF_I64)
#define PM_PYEXPORT_I64_PTR(i) (void *)pm_wasmmod_pyexport_i64_##i,
static void *const g_pyexport_i64_ptrs[PM_PYEXPORT_I64_POOL] = {
    PM_PYEXPORT_I64_IDX(PM_PYEXPORT_I64_PTR)
};

/* ---- f32 (1 arg) ---- */
#define PM_PYEXPORT_F32_POOL 4
static pm_pyexport_slot_t g_pyexport_f32[PM_PYEXPORT_F32_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_f32_callables[PM_PYEXPORT_F32_POOL]);
#define PM_PYEXPORT_DEF_F32(i) \
    static float pm_wasmmod_pyexport_f32_##i(float a0) { \
        if (!g_pyexport_f32[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_f32_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[1] = { mp_obj_new_float((mp_float_t)a0) }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 1, 0, args); \
            float out = (float)mp_obj_get_float(res); \
            nlr_pop(); \
            return out; \
        } \
        return 0; \
    }
#define PM_PYEXPORT_F32_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_F32_IDX(PM_PYEXPORT_DEF_F32)
#define PM_PYEXPORT_F32_PTR(i) (void *)pm_wasmmod_pyexport_f32_##i,
static void *const g_pyexport_f32_ptrs[PM_PYEXPORT_F32_POOL] = {
    PM_PYEXPORT_F32_IDX(PM_PYEXPORT_F32_PTR)
};

/* ---- f64 (1 arg) ---- */
#define PM_PYEXPORT_F64_POOL 4
static pm_pyexport_slot_t g_pyexport_f64[PM_PYEXPORT_F64_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_f64_callables[PM_PYEXPORT_F64_POOL]);
#define PM_PYEXPORT_DEF_F64(i) \
    static double pm_wasmmod_pyexport_f64_##i(double a0) { \
        if (!g_pyexport_f64[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_f64_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[1] = { mp_obj_new_float((mp_float_t)a0) }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 1, 0, args); \
            double out = (double)mp_obj_get_float(res); \
            nlr_pop(); \
            return out; \
        } \
        return 0; \
    }
#define PM_PYEXPORT_F64_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_F64_IDX(PM_PYEXPORT_DEF_F64)
#define PM_PYEXPORT_F64_PTR(i) (void *)pm_wasmmod_pyexport_f64_##i,
static void *const g_pyexport_f64_ptrs[PM_PYEXPORT_F64_POOL] = {
    PM_PYEXPORT_F64_IDX(PM_PYEXPORT_F64_PTR)
};

/* ---- raw host pointer + len -> bytes (ELF/native) ---- */
#define PM_PYEXPORT_BUFPTR_POOL 4
static pm_pyexport_slot_t g_pyexport_bufptr[PM_PYEXPORT_BUFPTR_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_bufptr_callables[PM_PYEXPORT_BUFPTR_POOL]);
#define PM_PYEXPORT_DEF_BUFPTR(i) \
    static int32_t pm_wasmmod_pyexport_bufptr_##i(const uint8_t *ptr, uint32_t len) { \
        if (!g_pyexport_bufptr[i].used || (len > 0 && ptr == NULL)) { \
            return -1; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_bufptr_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t buf = mp_obj_new_bytes(ptr == NULL ? (const byte *)"" : (const byte *)ptr, (size_t)len); \
            mp_obj_t res = mp_call_function_1(cb, buf); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
#define PM_PYEXPORT_BUFPTR_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_BUFPTR_IDX(PM_PYEXPORT_DEF_BUFPTR)
#define PM_PYEXPORT_BUFPTR_PTR(i) (void *)pm_wasmmod_pyexport_bufptr_##i,
static void *const g_pyexport_bufptr_ptrs[PM_PYEXPORT_BUFPTR_POOL] = {
    PM_PYEXPORT_BUFPTR_IDX(PM_PYEXPORT_BUFPTR_PTR)
};

/* ---- mem cookie → bytes ---- */
#define PM_PYEXPORT_MEM_POOL 4
static pm_pyexport_slot_t g_pyexport_mem[PM_PYEXPORT_MEM_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_mem_callables[PM_PYEXPORT_MEM_POOL]);
#define PM_PYEXPORT_DEF_MEM(i) \
    static int32_t pm_wasmmod_pyexport_mem_##i(pm_wasmmod_mem_cookie_t cookie) { \
        if (!g_pyexport_mem[i].used) { \
            return -1; \
        } \
        const uint8_t *ptr = NULL; \
        uint32_t len = 0; \
        if (pm_wasmmod_mem_cookie_get(cookie, &ptr, &len) != 0) { \
            return -1; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_mem_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t buf = mp_obj_new_bytes(ptr == NULL ? (const byte *)"" : (const byte *)ptr, \
                (size_t)len); \
            mp_obj_t res = mp_call_function_1(cb, buf); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
#define PM_PYEXPORT_MEM_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_MEM_IDX(PM_PYEXPORT_DEF_MEM)
#define PM_PYEXPORT_MEM_PTR(i) (void *)pm_wasmmod_pyexport_mem_##i,
static void *const g_pyexport_mem_ptrs[PM_PYEXPORT_MEM_POOL] = {
    PM_PYEXPORT_MEM_IDX(PM_PYEXPORT_MEM_PTR)
};

/* ---- obj handle → Python object ---- */
#define PM_PYEXPORT_OBJ_POOL 4
static pm_pyexport_slot_t g_pyexport_obj[PM_PYEXPORT_OBJ_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_pyexport_obj_callables[PM_PYEXPORT_OBJ_POOL]);
#define PM_PYEXPORT_DEF_OBJ(i) \
    static int32_t pm_wasmmod_pyexport_obj_##i(pm_wasmmod_obj_handle_t handle) { \
        if (!g_pyexport_obj[i].used) { \
            return -1; \
        } \
        mp_obj_t arg = pm_wasmmod_obj_handle_get(handle); \
        if (arg == MP_OBJ_NULL) { \
            return -1; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_wasmmod_pyexport_obj_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t res = mp_call_function_1(cb, arg); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
#define PM_PYEXPORT_OBJ_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_OBJ_IDX(PM_PYEXPORT_DEF_OBJ)
#define PM_PYEXPORT_OBJ_PTR(i) (void *)pm_wasmmod_pyexport_obj_##i,
static void *const g_pyexport_obj_ptrs[PM_PYEXPORT_OBJ_POOL] = {
    PM_PYEXPORT_OBJ_IDX(PM_PYEXPORT_OBJ_PTR)
};

static void *pyexport_acquire(pm_pyexport_slot_t *pool, mp_obj_t *callables, void *const *ptrs,
    uint32_t n, mp_obj_t callable) {
    for (uint32_t i = 0; i < n; i++) {
        if (!pool[i].used) {
            callables[i] = callable;
            pool[i].used = true;
            return ptrs[i];
        }
    }
    return NULL;
}

static int pyexport_publish(const char *fqn, const char *export_name, void *fn,
    pm_wasmmod_registry_export_kind_t kind, const char *sig) {
    if (fqn == NULL || export_name == NULL || fn == NULL || sig == NULL) {
        return -1;
    }
    size_t flen = strlen(fqn);
    size_t elen = strlen(export_name);
    size_t slen = strlen(sig);
    int32_t ok = pm_wasmmod_registry_mod_export(
        (const uint8_t *)fqn, (uint32_t)flen,
        (const uint8_t *)export_name, (uint32_t)elen,
        kind, fn,
        (const uint8_t *)sig, (uint32_t)slen);
    return ok ? 0 : -1;
}

int pm_wasmmod_pyexport_export_py(const char *fqn, const char *export_name,
    mp_obj_t callable, uint32_t nargs) {
    if (fqn == NULL || export_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = NULL;
    const char *sig = NULL;
    switch (nargs) {
        case 0:
            fn = pyexport_acquire(g_pyexport_v, MP_STATE_VM(pm_wasmmod_pyexport_v_callables),
                g_pyexport_v_ptrs, PM_PYEXPORT_V_POOL, callable);
            sig = "int32_t(void)";
            break;
        case 1:
            fn = pyexport_acquire(g_pyexport_1, MP_STATE_VM(pm_wasmmod_pyexport_1_callables),
                g_pyexport_1_ptrs, PM_PYEXPORT_1_POOL, callable);
            sig = "int32_t(int32_t)";
            break;
        case 2:
            fn = pyexport_acquire(g_pyexport_2, MP_STATE_VM(pm_wasmmod_pyexport_2_callables),
                g_pyexport_2_ptrs, PM_PYEXPORT_2_POOL, callable);
            sig = "int32_t(int32_t, int32_t)";
            break;
        case 3:
            fn = pyexport_acquire(g_pyexport_3, MP_STATE_VM(pm_wasmmod_pyexport_3_callables),
                g_pyexport_3_ptrs, PM_PYEXPORT_3_POOL, callable);
            sig = "int32_t(int32_t, int32_t, int32_t)";
            break;
        default:
            return -1;
    }
    if (fn == NULL) {
        return -1;
    }
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_FN, sig);
}

int pm_wasmmod_pyexport_export_py_i64(const char *fqn, const char *export_name, mp_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_i64, MP_STATE_VM(pm_wasmmod_pyexport_i64_callables),
        g_pyexport_i64_ptrs, PM_PYEXPORT_I64_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_I64, "int64_t(int64_t)");
}

int pm_wasmmod_pyexport_export_py_f32(const char *fqn, const char *export_name, mp_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_f32, MP_STATE_VM(pm_wasmmod_pyexport_f32_callables),
        g_pyexport_f32_ptrs, PM_PYEXPORT_F32_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_F32, "float(float)");
}

int pm_wasmmod_pyexport_export_py_f64(const char *fqn, const char *export_name, mp_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_f64, MP_STATE_VM(pm_wasmmod_pyexport_f64_callables),
        g_pyexport_f64_ptrs, PM_PYEXPORT_F64_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_F64, "double(double)");
}

int pm_wasmmod_pyexport_export_py_bufptr(const char *fqn, const char *export_name, mp_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_bufptr, MP_STATE_VM(pm_wasmmod_pyexport_bufptr_callables),
        g_pyexport_bufptr_ptrs, PM_PYEXPORT_BUFPTR_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_BUFPTR,
        "int32_t(const uint8_t *, uint32_t)");
}

int pm_wasmmod_pyexport_export_py_mem(const char *fqn, const char *export_name, mp_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_mem, MP_STATE_VM(pm_wasmmod_pyexport_mem_callables),
        g_pyexport_mem_ptrs, PM_PYEXPORT_MEM_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_MEM,
        "int32_t(pm_wasmmod_mem_cookie_t)");
}

int pm_wasmmod_pyexport_export_py_obj(const char *fqn, const char *export_name, mp_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_obj, MP_STATE_VM(pm_wasmmod_pyexport_obj_callables),
        g_pyexport_obj_ptrs, PM_PYEXPORT_OBJ_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_OBJ,
        "int32_t(pm_wasmmod_obj_handle_t)");
}

/* ---- bind helpers ---- */

static int pyexport_bind_one_sig(const char *fqn, const char *export_name, const char *sig,
    mp_obj_t callable) {
    if (strcmp(sig, "int32_t(void)") == 0) {
        return pm_wasmmod_pyexport_export_py(fqn, export_name, callable, 0);
    }
    if (strcmp(sig, "int32_t(int32_t)") == 0) {
        return pm_wasmmod_pyexport_export_py(fqn, export_name, callable, 1);
    }
    if (strcmp(sig, "int32_t(int32_t, int32_t)") == 0) {
        return pm_wasmmod_pyexport_export_py(fqn, export_name, callable, 2);
    }
    if (strcmp(sig, "int32_t(int32_t, int32_t, int32_t)") == 0) {
        return pm_wasmmod_pyexport_export_py(fqn, export_name, callable, 3);
    }
    if (strcmp(sig, "int64_t(int64_t)") == 0) {
        return pm_wasmmod_pyexport_export_py_i64(fqn, export_name, callable);
    }
    if (strcmp(sig, "float(float)") == 0) {
        return pm_wasmmod_pyexport_export_py_f32(fqn, export_name, callable);
    }
    if (strcmp(sig, "double(double)") == 0) {
        return pm_wasmmod_pyexport_export_py_f64(fqn, export_name, callable);
    }
    if (strcmp(sig, "int32_t(const uint8_t *, uint32_t)") == 0) {
        return pm_wasmmod_pyexport_export_py_bufptr(fqn, export_name, callable);
    }
    if (strcmp(sig, "int32_t(pm_wasmmod_mem_cookie_t)") == 0) {
        return pm_wasmmod_pyexport_export_py_mem(fqn, export_name, callable);
    }
    if (strcmp(sig, "int32_t(pm_wasmmod_obj_handle_t)") == 0) {
        return pm_wasmmod_pyexport_export_py_obj(fqn, export_name, callable);
    }
    return -1;
}

/* pymergetic.util.pysample → "pm_util_pysample_" */
static int fqn_to_c_prefix(const char *fqn, char *out, size_t out_sz) {
    if (fqn == NULL || out == NULL || out_sz < 8) {
        return -1;
    }
    const char *rest = fqn;
    if (strncmp(fqn, "pymergetic.", 11) == 0) {
        rest = fqn + 11;
    }
    size_t o = 0;
    if (o + 3 >= out_sz) {
        return -1;
    }
    out[o++] = 'p';
    out[o++] = 'm';
    out[o++] = '_';
    for (const char *p = rest; *p; ++p) {
        if (o + 2 >= out_sz) {
            return -1;
        }
        out[o++] = (*p == '.') ? '_' : *p;
    }
    if (o + 2 >= out_sz) {
        return -1;
    }
    out[o++] = '_';
    out[o] = '\0';
    return 0;
}

static mp_obj_t pyexport_lookup_attr(mp_obj_t module, const char *fqn, const char *export_name) {
    char prefix[160];
    if (fqn_to_c_prefix(fqn, prefix, sizeof(prefix)) != 0) {
        return MP_OBJ_NULL;
    }
    size_t plen = strlen(prefix);
    if (strncmp(export_name, prefix, plen) != 0) {
        return MP_OBJ_NULL;
    }
    const char *py_name = export_name + plen;
    if (py_name[0] == '\0') {
        return MP_OBJ_NULL;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return MP_OBJ_NULL;
    }
    mp_obj_t attr = mp_load_attr(module, qstr_from_str(py_name));
    nlr_pop();
    return attr;
}

static int pyexport_bind_named(const char *fqn, mp_obj_t module, const char *export_name,
    const char *sig) {
    mp_obj_t attr = pyexport_lookup_attr(module, fqn, export_name);
    if (attr == MP_OBJ_NULL || !mp_obj_is_callable(attr)) {
        return -1;
    }
    return pyexport_bind_one_sig(fqn, export_name, sig, attr);
}

static int pyexport_bind_from_registry(const char *fqn, mp_obj_t module) {
    size_t flen = strlen(fqn);
    uint32_t n = pm_wasmmod_registry_export_count((const uint8_t *)fqn, (uint32_t)flen);
    int bound = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t name[128];
        uint32_t name_len = sizeof(name);
        uint8_t sig[128];
        uint32_t sig_len = sizeof(sig);
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (pm_wasmmod_registry_export_at((const uint8_t *)fqn, (uint32_t)flen, i,
                name, &name_len, &kind, sig, &sig_len) == 0) {
            continue;
        }
        name[name_len < sizeof(name) ? name_len : sizeof(name) - 1] = '\0';
        sig[sig_len < sizeof(sig) ? sig_len : sizeof(sig) - 1] = '\0';
        if (sig_len == 0) {
            continue;
        }
        if (pyexport_bind_named(fqn, module, (const char *)name, (const char *)sig) == 0) {
            bound++;
        }
    }
    return bound;
}

/* Parse facegen `__exports__.h` prototypes: `ret name(args);` */
static int pyexport_bind_from_exports_h(const char *fqn, mp_obj_t module, const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    char line[256];
    int bound = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (strncmp(p, "int32_t ", 8) != 0 && strncmp(p, "int64_t ", 8) != 0
            && strncmp(p, "float ", 6) != 0 && strncmp(p, "double ", 7) != 0) {
            continue;
        }
        char *name_start = strchr(p, ' ');
        if (name_start == NULL) {
            continue;
        }
        while (*name_start == ' ') {
            name_start++;
        }
        char *paren = strchr(name_start, '(');
        if (paren == NULL) {
            continue;
        }
        char *semi = strchr(paren, ';');
        if (semi == NULL) {
            continue;
        }
        *paren = '\0';
        /* args run from after '(' up to matching ')' before ';' */
        char *args_start = paren + 1;
        char *args_end = semi;
        while (args_end > args_start && args_end[-1] != ')') {
            args_end--;
        }
        if (args_end > args_start && args_end[-1] == ')') {
            args_end--;
        }
        *args_end = '\0';
        /* trim name */
        char *name_end = paren;
        while (name_end > name_start && (name_end[-1] == ' ' || name_end[-1] == '\t')) {
            name_end--;
        }
        *name_end = '\0';
        /* rebuild "ret(args)" sig from original ret token + args */
        char sig[160];
        char ret[16];
        if (strncmp(p, "int32_t ", 8) == 0) {
            strcpy(ret, "int32_t");
        } else if (strncmp(p, "int64_t ", 8) == 0) {
            strcpy(ret, "int64_t");
        } else if (strncmp(p, "float ", 6) == 0) {
            strcpy(ret, "float");
        } else {
            strcpy(ret, "double");
        }
        const char *args = args_start;
        while (*args == ' ') {
            args++;
        }
        size_t alen = strlen(args);
        while (alen > 0 && (args[alen - 1] == ' ' || args[alen - 1] == '\t')) {
            alen--;
        }
        if (snprintf(sig, sizeof(sig), "%s(%.*s)", ret, (int)alen, args) >= (int)sizeof(sig)) {
            continue;
        }
        if (pyexport_bind_named(fqn, module, name_start, sig) == 0) {
            bound++;
        }
    }
    fclose(f);
    return bound;
}

static int pyexport_try_exports_h(const char *fqn, mp_obj_t module) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return 0;
    }
    mp_obj_t file_obj = mp_load_attr(module, qstr_from_str("__file__"));
    nlr_pop();
    if (!mp_obj_is_str(file_obj)) {
        return 0;
    }
    const char *file = mp_obj_str_get_str(file_obj);
    /* .../__init__.py → .../__exports__.h */
    size_t n = strlen(file);
    char path[512];
    if (n + 16 >= sizeof(path)) {
        return 0;
    }
    memcpy(path, file, n + 1);
    char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return 0;
    }
    strcpy(slash + 1, "__exports__.h");
    return pyexport_bind_from_exports_h(fqn, module, path);
}

int pm_wasmmod_pyexport_bind_module(const char *fqn, mp_obj_t module) {
    if (fqn == NULL || module == MP_OBJ_NULL) {
        return -1;
    }
    int bound = pyexport_bind_from_registry(fqn, module);
    if (bound > 0) {
        return bound;
    }
    bound = pyexport_try_exports_h(fqn, module);
    return bound;
}

#else /* !MICROPY_PY_WASM — stubs when neither µPy nor CPython port compiles this TU */

#include "pymergetic/wasmmod/pyexport/__exports__.h"

int pm_wasmmod_pyexport_export_py(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable, uint32_t nargs) {
    (void)fqn;
    (void)export_name;
    (void)callable;
    (void)nargs;
    return -1;
}

int pm_wasmmod_pyexport_export_py_i64(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    (void)fqn;
    (void)export_name;
    (void)callable;
    return -1;
}

int pm_wasmmod_pyexport_export_py_f32(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    (void)fqn;
    (void)export_name;
    (void)callable;
    return -1;
}

int pm_wasmmod_pyexport_export_py_f64(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    (void)fqn;
    (void)export_name;
    (void)callable;
    return -1;
}

int pm_wasmmod_pyexport_export_py_bufptr(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    (void)fqn;
    (void)export_name;
    (void)callable;
    return -1;
}

int pm_wasmmod_pyexport_export_py_mem(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    (void)fqn;
    (void)export_name;
    (void)callable;
    return -1;
}

int pm_wasmmod_pyexport_export_py_obj(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    (void)fqn;
    (void)export_name;
    (void)callable;
    return -1;
}

int pm_wasmmod_pyexport_bind_module(const char *fqn, pm_wasmmod_py_obj_t module) {
    (void)fqn;
    (void)module;
    return -1;
}

#endif /* MICROPY_PY_WASM */
