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
 * See pyexport.h. Each pool below is a fixed array of {callable, used} slots
 * (callables are GC-rooted — a slot's Python object must stay alive as long
 * as something might still resolve/call it) plus exactly that many real,
 * distinct C functions (X-macro generated): a slot's *identity* (its
 * function address) is what a native caller resolves and calls, so
 * concurrently-exported callables of the same arity genuinely need distinct
 * compiled functions, not just distinct data — same reasoning as thunk.c.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "extmod/wasmmod/pyexport.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "extmod/wasmmod/host.h"
#include "pm_mod.h"
#include "py/obj.h"
#include "py/runtime.h"

typedef struct {
    bool used;
} pm_pyexport_slot_t;

/* ---- arity: 0 ---- */
#define PM_PYEXPORT_V_POOL 8
static pm_pyexport_slot_t g_pyexport_v[PM_PYEXPORT_V_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_v_callables[PM_PYEXPORT_V_POOL]);
#define PM_PYEXPORT_DEF_V(i) \
    static int32_t pm_pyexport_v_##i(void) { \
        if (!g_pyexport_v[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_v_callables)[i]; \
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
#define PM_PYEXPORT_V_PTR(i) (void *)pm_pyexport_v_##i,
static void *const g_pyexport_v_ptrs[PM_PYEXPORT_V_POOL] = {
    PM_PYEXPORT_V_IDX(PM_PYEXPORT_V_PTR)
};

/* ---- arity: 1 ---- */
#define PM_PYEXPORT_1_POOL 8
static pm_pyexport_slot_t g_pyexport_1[PM_PYEXPORT_1_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_1_callables[PM_PYEXPORT_1_POOL]);
#define PM_PYEXPORT_DEF_1(i) \
    static int32_t pm_pyexport_1_##i(int32_t a0) { \
        if (!g_pyexport_1[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_1_callables)[i]; \
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
#define PM_PYEXPORT_1_PTR(i) (void *)pm_pyexport_1_##i,
static void *const g_pyexport_1_ptrs[PM_PYEXPORT_1_POOL] = {
    PM_PYEXPORT_1_IDX(PM_PYEXPORT_1_PTR)
};

/* ---- arity: 2 ---- */
#define PM_PYEXPORT_2_POOL 4
static pm_pyexport_slot_t g_pyexport_2[PM_PYEXPORT_2_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_2_callables[PM_PYEXPORT_2_POOL]);
#define PM_PYEXPORT_DEF_2(i) \
    static int32_t pm_pyexport_2_##i(int32_t a0, int32_t a1) { \
        if (!g_pyexport_2[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_2_callables)[i]; \
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
#define PM_PYEXPORT_2_PTR(i) (void *)pm_pyexport_2_##i,
static void *const g_pyexport_2_ptrs[PM_PYEXPORT_2_POOL] = {
    PM_PYEXPORT_2_IDX(PM_PYEXPORT_2_PTR)
};

/* ---- arity: 3 ---- */
#define PM_PYEXPORT_3_POOL 4
static pm_pyexport_slot_t g_pyexport_3[PM_PYEXPORT_3_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_3_callables[PM_PYEXPORT_3_POOL]);
#define PM_PYEXPORT_DEF_3(i) \
    static int32_t pm_pyexport_3_##i(int32_t a0, int32_t a1, int32_t a2) { \
        if (!g_pyexport_3[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_3_callables)[i]; \
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
#define PM_PYEXPORT_3_PTR(i) (void *)pm_pyexport_3_##i,
static void *const g_pyexport_3_ptrs[PM_PYEXPORT_3_POOL] = {
    PM_PYEXPORT_3_IDX(PM_PYEXPORT_3_PTR)
};

/* ---- i64 (1 arg) ---- */
#define PM_PYEXPORT_I64_POOL 4
static pm_pyexport_slot_t g_pyexport_i64[PM_PYEXPORT_I64_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_i64_callables[PM_PYEXPORT_I64_POOL]);
#define PM_PYEXPORT_DEF_I64(i) \
    static int64_t pm_pyexport_i64_##i(int64_t a0) { \
        if (!g_pyexport_i64[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_i64_callables)[i]; \
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
#define PM_PYEXPORT_I64_PTR(i) (void *)pm_pyexport_i64_##i,
static void *const g_pyexport_i64_ptrs[PM_PYEXPORT_I64_POOL] = {
    PM_PYEXPORT_I64_IDX(PM_PYEXPORT_I64_PTR)
};

/* ---- f32 (1 arg) ---- */
#define PM_PYEXPORT_F32_POOL 4
static pm_pyexport_slot_t g_pyexport_f32[PM_PYEXPORT_F32_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_f32_callables[PM_PYEXPORT_F32_POOL]);
#define PM_PYEXPORT_DEF_F32(i) \
    static float pm_pyexport_f32_##i(float a0) { \
        if (!g_pyexport_f32[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_f32_callables)[i]; \
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
#define PM_PYEXPORT_F32_PTR(i) (void *)pm_pyexport_f32_##i,
static void *const g_pyexport_f32_ptrs[PM_PYEXPORT_F32_POOL] = {
    PM_PYEXPORT_F32_IDX(PM_PYEXPORT_F32_PTR)
};

/* ---- f64 (1 arg) ---- */
#define PM_PYEXPORT_F64_POOL 4
static pm_pyexport_slot_t g_pyexport_f64[PM_PYEXPORT_F64_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_f64_callables[PM_PYEXPORT_F64_POOL]);
#define PM_PYEXPORT_DEF_F64(i) \
    static double pm_pyexport_f64_##i(double a0) { \
        if (!g_pyexport_f64[i].used) { \
            return 0; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_f64_callables)[i]; \
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
#define PM_PYEXPORT_F64_PTR(i) (void *)pm_pyexport_f64_##i,
static void *const g_pyexport_f64_ptrs[PM_PYEXPORT_F64_POOL] = {
    PM_PYEXPORT_F64_IDX(PM_PYEXPORT_F64_PTR)
};

/* ---- mem cookie -> bytes (1 arg) ---- */
#define PM_PYEXPORT_MEM_POOL 4
static pm_pyexport_slot_t g_pyexport_mem[PM_PYEXPORT_MEM_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_mem_callables[PM_PYEXPORT_MEM_POOL]);
#define PM_PYEXPORT_DEF_MEM(i) \
    static int32_t pm_pyexport_mem_##i(int32_t cookie) { \
        if (!g_pyexport_mem[i].used) { \
            return -1; \
        } \
        uint32_t len = 0; \
        const uint8_t *data = mp_wasm_mem_data(cookie, &len); \
        if (data == NULL && len != 0) { \
            return -1; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_mem_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t buf = mp_obj_new_bytes(data == NULL ? (const uint8_t *)"" : data, (size_t)len); \
            mp_obj_t res = mp_call_function_1(cb, buf); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
#define PM_PYEXPORT_MEM_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_MEM_IDX(PM_PYEXPORT_DEF_MEM)
#define PM_PYEXPORT_MEM_PTR(i) (void *)pm_pyexport_mem_##i,
static void *const g_pyexport_mem_ptrs[PM_PYEXPORT_MEM_POOL] = {
    PM_PYEXPORT_MEM_IDX(PM_PYEXPORT_MEM_PTR)
};

/* ---- object handle -> resolved mp_obj_t (1 arg) ---- */
#define PM_PYEXPORT_OBJ_POOL 4
static pm_pyexport_slot_t g_pyexport_obj[PM_PYEXPORT_OBJ_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_obj_callables[PM_PYEXPORT_OBJ_POOL]);
#define PM_PYEXPORT_DEF_OBJ(i) \
    static int32_t pm_pyexport_obj_##i(int32_t handle) { \
        if (!g_pyexport_obj[i].used || handle <= 0) { \
            return -1; \
        } \
        mp_obj_t obj = mp_wasm_handle_resolve(handle); \
        if (obj == mp_const_none) { \
            return -1; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_obj_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t res = mp_call_function_1(cb, obj); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
#define PM_PYEXPORT_OBJ_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_OBJ_IDX(PM_PYEXPORT_DEF_OBJ)
#define PM_PYEXPORT_OBJ_PTR(i) (void *)pm_pyexport_obj_##i,
static void *const g_pyexport_obj_ptrs[PM_PYEXPORT_OBJ_POOL] = {
    PM_PYEXPORT_OBJ_IDX(PM_PYEXPORT_OBJ_PTR)
};

/* ---- raw host pointer + len -> bytes (ELF/native guests only) ---- */
#define PM_PYEXPORT_BUFPTR_POOL 4
static pm_pyexport_slot_t g_pyexport_bufptr[PM_PYEXPORT_BUFPTR_POOL];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_pyexport_bufptr_callables[PM_PYEXPORT_BUFPTR_POOL]);
#define PM_PYEXPORT_DEF_BUFPTR(i) \
    static int32_t pm_pyexport_bufptr_##i(const void *ptr, int32_t len) { \
        if (!g_pyexport_bufptr[i].used || len < 0 || (len > 0 && ptr == NULL)) { \
            return -1; \
        } \
        mp_obj_t cb = MP_STATE_VM(pm_pyexport_bufptr_callables)[i]; \
        nlr_buf_t nlr; \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t buf = mp_obj_new_bytes(ptr == NULL ? (const uint8_t *)"" : (const uint8_t *)ptr, (size_t)len); \
            mp_obj_t res = mp_call_function_1(cb, buf); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
#define PM_PYEXPORT_BUFPTR_IDX(X) X(0) X(1) X(2) X(3)
PM_PYEXPORT_BUFPTR_IDX(PM_PYEXPORT_DEF_BUFPTR)
#define PM_PYEXPORT_BUFPTR_PTR(i) (void *)pm_pyexport_bufptr_##i,
static void *const g_pyexport_bufptr_ptrs[PM_PYEXPORT_BUFPTR_POOL] = {
    PM_PYEXPORT_BUFPTR_IDX(PM_PYEXPORT_BUFPTR_PTR)
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

int pm_mod_export_py(const char *module_name, const char *func_name,
    mp_obj_t callable, uint32_t nargs) {
    if (module_name == NULL || func_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = NULL;
    switch (nargs) {
        case 0:
            fn = pyexport_acquire(g_pyexport_v, MP_STATE_VM(pm_pyexport_v_callables),
                g_pyexport_v_ptrs, PM_PYEXPORT_V_POOL, callable);
            break;
        case 1:
            fn = pyexport_acquire(g_pyexport_1, MP_STATE_VM(pm_pyexport_1_callables),
                g_pyexport_1_ptrs, PM_PYEXPORT_1_POOL, callable);
            break;
        case 2:
            fn = pyexport_acquire(g_pyexport_2, MP_STATE_VM(pm_pyexport_2_callables),
                g_pyexport_2_ptrs, PM_PYEXPORT_2_POOL, callable);
            break;
        case 3:
            fn = pyexport_acquire(g_pyexport_3, MP_STATE_VM(pm_pyexport_3_callables),
                g_pyexport_3_ptrs, PM_PYEXPORT_3_POOL, callable);
            break;
        default:
            return -1;
    }
    if (fn == NULL) {
        return -1;
    }
    if (!pm_mod_has(module_name)) {
        (void)pm_mod_publish(module_name, PM_MOD_RESIDENT, NULL, 0);
    }
    return pm_mod_export_set(module_name, func_name, fn);
}

static int pyexport_publish(const char *module_name, const char *func_name, void *fn) {
    if (fn == NULL) {
        return -1;
    }
    if (!pm_mod_has(module_name)) {
        (void)pm_mod_publish(module_name, PM_MOD_RESIDENT, NULL, 0);
    }
    return pm_mod_export_set(module_name, func_name, fn);
}

int pm_mod_export_py_i64(const char *module_name, const char *func_name, mp_obj_t callable) {
    if (module_name == NULL || func_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_i64, MP_STATE_VM(pm_pyexport_i64_callables),
        g_pyexport_i64_ptrs, PM_PYEXPORT_I64_POOL, callable);
    return pyexport_publish(module_name, func_name, fn);
}

int pm_mod_export_py_f32(const char *module_name, const char *func_name, mp_obj_t callable) {
    if (module_name == NULL || func_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_f32, MP_STATE_VM(pm_pyexport_f32_callables),
        g_pyexport_f32_ptrs, PM_PYEXPORT_F32_POOL, callable);
    return pyexport_publish(module_name, func_name, fn);
}

int pm_mod_export_py_f64(const char *module_name, const char *func_name, mp_obj_t callable) {
    if (module_name == NULL || func_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_f64, MP_STATE_VM(pm_pyexport_f64_callables),
        g_pyexport_f64_ptrs, PM_PYEXPORT_F64_POOL, callable);
    return pyexport_publish(module_name, func_name, fn);
}

int pm_mod_export_py_mem(const char *module_name, const char *func_name, mp_obj_t callable) {
    if (module_name == NULL || func_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_mem, MP_STATE_VM(pm_pyexport_mem_callables),
        g_pyexport_mem_ptrs, PM_PYEXPORT_MEM_POOL, callable);
    return pyexport_publish(module_name, func_name, fn);
}

int pm_mod_export_py_obj(const char *module_name, const char *func_name, mp_obj_t callable) {
    if (module_name == NULL || func_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_obj, MP_STATE_VM(pm_pyexport_obj_callables),
        g_pyexport_obj_ptrs, PM_PYEXPORT_OBJ_POOL, callable);
    return pyexport_publish(module_name, func_name, fn);
}

int pm_mod_export_py_bufptr(const char *module_name, const char *func_name, mp_obj_t callable) {
    if (module_name == NULL || func_name == NULL || callable == MP_OBJ_NULL
        || !mp_obj_is_callable(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_bufptr, MP_STATE_VM(pm_pyexport_bufptr_callables),
        g_pyexport_bufptr_ptrs, PM_PYEXPORT_BUFPTR_POOL, callable);
    return pyexport_publish(module_name, func_name, fn);
}

#else /* !MICROPY_PY_WASM */

#include "extmod/wasmmod/pyexport.h"

int pm_mod_export_py(const char *module_name, const char *func_name,
    mp_obj_t callable, uint32_t nargs) {
    (void)module_name;
    (void)func_name;
    (void)callable;
    (void)nargs;
    return -1;
}

int pm_mod_export_py_i64(const char *module_name, const char *func_name, mp_obj_t callable) {
    (void)module_name;
    (void)func_name;
    (void)callable;
    return -1;
}

int pm_mod_export_py_f32(const char *module_name, const char *func_name, mp_obj_t callable) {
    (void)module_name;
    (void)func_name;
    (void)callable;
    return -1;
}

int pm_mod_export_py_f64(const char *module_name, const char *func_name, mp_obj_t callable) {
    (void)module_name;
    (void)func_name;
    (void)callable;
    return -1;
}

int pm_mod_export_py_mem(const char *module_name, const char *func_name, mp_obj_t callable) {
    (void)module_name;
    (void)func_name;
    (void)callable;
    return -1;
}

int pm_mod_export_py_obj(const char *module_name, const char *func_name, mp_obj_t callable) {
    (void)module_name;
    (void)func_name;
    (void)callable;
    return -1;
}

int pm_mod_export_py_bufptr(const char *module_name, const char *func_name, mp_obj_t callable) {
    (void)module_name;
    (void)func_name;
    (void)callable;
    return -1;
}

#endif /* MICROPY_PY_WASM */
