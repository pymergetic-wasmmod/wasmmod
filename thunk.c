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
 * See thunk.h. Every pool below is a fixed array of {pack, cached wasm
 * function, used} slots plus exactly that many real, distinct C functions
 * (X-macro generated) — a slot's *identity* (its function address) is what
 * a native caller resolves and calls, so concurrently-thunked exports of
 * the same shape genuinely need distinct compiled functions, not just
 * distinct data: the real signature (e.g. int32_t(int32_t,int32_t,int32_t))
 * has no room for a hidden "which slot" argument.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "extmod/wasmmod/thunk.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "extmod/wasmmod/alloc.h"
#include "pm_mod.h"

typedef struct {
    mp_pack_t *pack;
    wasm_function_inst_t fn;
    bool used;
} pm_thunk_slot_t;

static void *thunk_pool_acquire(pm_thunk_slot_t *pool, void *const *ptrs, uint32_t n,
    mp_pack_t *wmod, wasm_function_inst_t fn) {
    for (uint32_t i = 0; i < n; i++) {
        if (!pool[i].used) {
            pool[i].pack = wmod;
            pool[i].fn = fn;
            pool[i].used = true;
            return ptrs[i];
        }
    }
    return NULL;
}

static void thunk_pool_release(pm_thunk_slot_t *pool, uint32_t n, mp_pack_t *wmod) {
    for (uint32_t i = 0; i < n; i++) {
        if (pool[i].used && pool[i].pack == wmod) {
            pool[i].used = false;
            pool[i].pack = NULL;
            pool[i].fn = NULL;
        }
    }
}

/* ---- shape: () -> i32  (hello(), mixed_answer(), via_*() ...) ---- */
#define PM_THUNK_V_I32_POOL 28
static pm_thunk_slot_t g_thunk_v_i32[PM_THUNK_V_I32_POOL];
#define PM_THUNK_DEF_V_I32(i) \
    static int32_t pm_thunk_v_i32_##i(void) { \
        pm_thunk_slot_t *s = &g_thunk_v_i32[i]; \
        if (!s->used) { \
            return 0; \
        } \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 0, NULL, 1, &res, NULL, 0)) { \
            return 0; \
        } \
        return res.of.i32; \
    }
#define PM_THUNK_V_I32_IDX(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) \
    X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) \
    X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27)
PM_THUNK_V_I32_IDX(PM_THUNK_DEF_V_I32)
#define PM_THUNK_V_I32_PTR(i) (void *)pm_thunk_v_i32_##i,
static void *const g_thunk_v_i32_ptrs[PM_THUNK_V_I32_POOL] = {
    PM_THUNK_V_I32_IDX(PM_THUNK_V_I32_PTR)
};

/* ---- shape: (i32) -> i32  (via_rs(x), matrix(x), rs_square(x) ...) ---- */
#define PM_THUNK_I32_I32_POOL 18
static pm_thunk_slot_t g_thunk_i32_i32[PM_THUNK_I32_I32_POOL];
#define PM_THUNK_DEF_I32_I32(i) \
    static int32_t pm_thunk_i32_i32_##i(int32_t a0) { \
        pm_thunk_slot_t *s = &g_thunk_i32_i32[i]; \
        if (!s->used) { \
            return 0; \
        } \
        wasm_val_t args[1] = { { .kind = WASM_I32, .of.i32 = a0 } }; \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 1, args, 1, &res, NULL, 0)) { \
            return 0; \
        } \
        return res.of.i32; \
    }
#define PM_THUNK_I32_I32_IDX(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) \
    X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17)
PM_THUNK_I32_I32_IDX(PM_THUNK_DEF_I32_I32)
#define PM_THUNK_I32_I32_PTR(i) (void *)pm_thunk_i32_i32_##i,
static void *const g_thunk_i32_i32_ptrs[PM_THUNK_I32_I32_POOL] = {
    PM_THUNK_I32_I32_IDX(PM_THUNK_I32_I32_PTR)
};

/* ---- shape: (i32,i32) -> i32  (add(a,b)) ---- */
#define PM_THUNK_I32X2_I32_POOL 2
static pm_thunk_slot_t g_thunk_i32x2_i32[PM_THUNK_I32X2_I32_POOL];
#define PM_THUNK_DEF_I32X2_I32(i) \
    static int32_t pm_thunk_i32x2_i32_##i(int32_t a0, int32_t a1) { \
        pm_thunk_slot_t *s = &g_thunk_i32x2_i32[i]; \
        if (!s->used) { \
            return 0; \
        } \
        wasm_val_t args[2] = { \
            { .kind = WASM_I32, .of.i32 = a0 }, \
            { .kind = WASM_I32, .of.i32 = a1 }, \
        }; \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 2, args, 1, &res, NULL, 0)) { \
            return 0; \
        } \
        return res.of.i32; \
    }
#define PM_THUNK_I32X2_I32_IDX(X) X(0) X(1)
PM_THUNK_I32X2_I32_IDX(PM_THUNK_DEF_I32X2_I32)
#define PM_THUNK_I32X2_I32_PTR(i) (void *)pm_thunk_i32x2_i32_##i,
static void *const g_thunk_i32x2_i32_ptrs[PM_THUNK_I32X2_I32_POOL] = {
    PM_THUNK_I32X2_I32_IDX(PM_THUNK_I32X2_I32_PTR)
};

/* ---- shape: (i32,i32,i32) -> i32  (add3(a,b,c), rs_add3(a,b,c)) ---- */
#define PM_THUNK_I32X3_I32_POOL 2
static pm_thunk_slot_t g_thunk_i32x3_i32[PM_THUNK_I32X3_I32_POOL];
#define PM_THUNK_DEF_I32X3_I32(i) \
    static int32_t pm_thunk_i32x3_i32_##i(int32_t a0, int32_t a1, int32_t a2) { \
        pm_thunk_slot_t *s = &g_thunk_i32x3_i32[i]; \
        if (!s->used) { \
            return 0; \
        } \
        wasm_val_t args[3] = { \
            { .kind = WASM_I32, .of.i32 = a0 }, \
            { .kind = WASM_I32, .of.i32 = a1 }, \
            { .kind = WASM_I32, .of.i32 = a2 }, \
        }; \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 3, args, 1, &res, NULL, 0)) { \
            return 0; \
        } \
        return res.of.i32; \
    }
#define PM_THUNK_I32X3_I32_IDX(X) X(0) X(1)
PM_THUNK_I32X3_I32_IDX(PM_THUNK_DEF_I32X3_I32)
#define PM_THUNK_I32X3_I32_PTR(i) (void *)pm_thunk_i32x3_i32_##i,
static void *const g_thunk_i32x3_i32_ptrs[PM_THUNK_I32X3_I32_POOL] = {
    PM_THUNK_I32X3_I32_IDX(PM_THUNK_I32X3_I32_PTR)
};

/* ---- shape: (i64) -> i64  (mixed_i64(x), via_i64(x), matrix_rich(x) ...) ---- */
#define PM_THUNK_I64_I64_POOL 12
static pm_thunk_slot_t g_thunk_i64_i64[PM_THUNK_I64_I64_POOL];
#define PM_THUNK_DEF_I64_I64(i) \
    static int64_t pm_thunk_i64_i64_##i(int64_t a0) { \
        pm_thunk_slot_t *s = &g_thunk_i64_i64[i]; \
        if (!s->used) { \
            return 0; \
        } \
        wasm_val_t args[1] = { { .kind = WASM_I64, .of.i64 = a0 } }; \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 1, args, 1, &res, NULL, 0)) { \
            return 0; \
        } \
        return res.of.i64; \
    }
#define PM_THUNK_I64_I64_IDX(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11)
PM_THUNK_I64_I64_IDX(PM_THUNK_DEF_I64_I64)
#define PM_THUNK_I64_I64_PTR(i) (void *)pm_thunk_i64_i64_##i,
static void *const g_thunk_i64_i64_ptrs[PM_THUNK_I64_I64_POOL] = {
    PM_THUNK_I64_I64_IDX(PM_THUNK_I64_I64_PTR)
};

/* ---- shape: (f32) -> f32  (via_f32(x), rs_via_f32(x) ...) ---- */
#define PM_THUNK_F32_F32_POOL 4
static pm_thunk_slot_t g_thunk_f32_f32[PM_THUNK_F32_F32_POOL];
#define PM_THUNK_DEF_F32_F32(i) \
    static float pm_thunk_f32_f32_##i(float a0) { \
        pm_thunk_slot_t *s = &g_thunk_f32_f32[i]; \
        if (!s->used) { \
            return 0.0f; \
        } \
        wasm_val_t args[1] = { { .kind = WASM_F32, .of.f32 = a0 } }; \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 1, args, 1, &res, NULL, 0)) { \
            return 0.0f; \
        } \
        return res.of.f32; \
    }
#define PM_THUNK_F32_F32_IDX(X) X(0) X(1) X(2) X(3)
PM_THUNK_F32_F32_IDX(PM_THUNK_DEF_F32_F32)
#define PM_THUNK_F32_F32_PTR(i) (void *)pm_thunk_f32_f32_##i,
static void *const g_thunk_f32_f32_ptrs[PM_THUNK_F32_F32_POOL] = {
    PM_THUNK_F32_F32_IDX(PM_THUNK_F32_F32_PTR)
};

/* ---- shape: (f64) -> f64  (via_f64(x), rs_via_f64(x) ...) ---- */
#define PM_THUNK_F64_F64_POOL 4
static pm_thunk_slot_t g_thunk_f64_f64[PM_THUNK_F64_F64_POOL];
#define PM_THUNK_DEF_F64_F64(i) \
    static double pm_thunk_f64_f64_##i(double a0) { \
        pm_thunk_slot_t *s = &g_thunk_f64_f64[i]; \
        if (!s->used) { \
            return 0.0; \
        } \
        wasm_val_t args[1] = { { .kind = WASM_F64, .of.f64 = a0 } }; \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 1, args, 1, &res, NULL, 0)) { \
            return 0.0; \
        } \
        return res.of.f64; \
    }
#define PM_THUNK_F64_F64_IDX(X) X(0) X(1) X(2) X(3)
PM_THUNK_F64_F64_IDX(PM_THUNK_DEF_F64_F64)
#define PM_THUNK_F64_F64_PTR(i) (void *)pm_thunk_f64_f64_##i,
static void *const g_thunk_f64_f64_ptrs[PM_THUNK_F64_F64_POOL] = {
    PM_THUNK_F64_F64_IDX(PM_THUNK_F64_F64_PTR)
};

/* ---- shape: (f64,f64,f64) -> f64  (scale_add_f64, rs_scale_add_f64) ---- */
#define PM_THUNK_F64X3_F64_POOL 2
static pm_thunk_slot_t g_thunk_f64x3_f64[PM_THUNK_F64X3_F64_POOL];
#define PM_THUNK_DEF_F64X3_F64(i) \
    static double pm_thunk_f64x3_f64_##i(double a0, double a1, double a2) { \
        pm_thunk_slot_t *s = &g_thunk_f64x3_f64[i]; \
        if (!s->used) { \
            return 0.0; \
        } \
        wasm_val_t args[3] = { \
            { .kind = WASM_F64, .of.f64 = a0 }, \
            { .kind = WASM_F64, .of.f64 = a1 }, \
            { .kind = WASM_F64, .of.f64 = a2 }, \
        }; \
        wasm_val_t res; \
        if (!mp_pack_call_fn(s->pack, s->fn, 3, args, 1, &res, NULL, 0)) { \
            return 0.0; \
        } \
        return res.of.f64; \
    }
#define PM_THUNK_F64X3_F64_IDX(X) X(0) X(1)
PM_THUNK_F64X3_F64_IDX(PM_THUNK_DEF_F64X3_F64)
#define PM_THUNK_F64X3_F64_PTR(i) (void *)pm_thunk_f64x3_f64_##i,
static void *const g_thunk_f64x3_f64_ptrs[PM_THUNK_F64X3_F64_POOL] = {
    PM_THUNK_F64X3_F64_IDX(PM_THUNK_F64X3_F64_PTR)
};

static bool kinds_all(const wasm_valkind_t *k, uint32_t n, wasm_valkind_t want) {
    for (uint32_t i = 0; i < n; i++) {
        if (k[i] != want) {
            return false;
        }
    }
    return true;
}

void pm_mod_thunk_export(mp_pack_t *wmod, const char *pm_module_name,
    const char *wasm_export_name, const char *pm_func_name) {
    if (wmod == NULL || pm_module_name == NULL || wasm_export_name == NULL || pm_func_name == NULL) {
        return;
    }
    uint32_t nparams = 0, nresults = 0;
    wasm_valkind_t *params = NULL, *results = NULL;
    if (!mp_pack_func_types(wmod, wasm_export_name, &nparams, &params, &nresults, &results)) {
        return;
    }
    void *thunk = NULL;
    if (nresults == 1) {
        wasm_function_inst_t fn = mp_pack_lookup_fn(wmod, wasm_export_name);
        if (fn != NULL) {
            if (results[0] == WASM_I32) {
                if (nparams == 0) {
                    thunk = thunk_pool_acquire(g_thunk_v_i32, g_thunk_v_i32_ptrs,
                        PM_THUNK_V_I32_POOL, wmod, fn);
                } else if (nparams == 1 && kinds_all(params, 1, WASM_I32)) {
                    thunk = thunk_pool_acquire(g_thunk_i32_i32, g_thunk_i32_i32_ptrs,
                        PM_THUNK_I32_I32_POOL, wmod, fn);
                } else if (nparams == 2 && kinds_all(params, 2, WASM_I32)) {
                    thunk = thunk_pool_acquire(g_thunk_i32x2_i32, g_thunk_i32x2_i32_ptrs,
                        PM_THUNK_I32X2_I32_POOL, wmod, fn);
                } else if (nparams == 3 && kinds_all(params, 3, WASM_I32)) {
                    thunk = thunk_pool_acquire(g_thunk_i32x3_i32, g_thunk_i32x3_i32_ptrs,
                        PM_THUNK_I32X3_I32_POOL, wmod, fn);
                }
            } else if (results[0] == WASM_I64 && nparams == 1 && kinds_all(params, 1, WASM_I64)) {
                thunk = thunk_pool_acquire(g_thunk_i64_i64, g_thunk_i64_i64_ptrs,
                    PM_THUNK_I64_I64_POOL, wmod, fn);
            } else if (results[0] == WASM_F32 && nparams == 1 && kinds_all(params, 1, WASM_F32)) {
                thunk = thunk_pool_acquire(g_thunk_f32_f32, g_thunk_f32_f32_ptrs,
                    PM_THUNK_F32_F32_POOL, wmod, fn);
            } else if (results[0] == WASM_F64) {
                if (nparams == 1 && kinds_all(params, 1, WASM_F64)) {
                    thunk = thunk_pool_acquire(g_thunk_f64_f64, g_thunk_f64_f64_ptrs,
                        PM_THUNK_F64_F64_POOL, wmod, fn);
                } else if (nparams == 3 && kinds_all(params, 3, WASM_F64)) {
                    thunk = thunk_pool_acquire(g_thunk_f64x3_f64, g_thunk_f64x3_f64_ptrs,
                        PM_THUNK_F64X3_F64_POOL, wmod, fn);
                }
            }
        }
    }
    if (params != NULL) {
        MICROPY_WASM_FREE(params);
    }
    if (results != NULL) {
        MICROPY_WASM_FREE(results);
    }
    if (thunk != NULL) {
        pm_mod_export_set(pm_module_name, pm_func_name, thunk);
    }
}

void pm_mod_thunk_release_pack(mp_pack_t *wmod) {
    if (wmod == NULL) {
        return;
    }
    thunk_pool_release(g_thunk_v_i32, PM_THUNK_V_I32_POOL, wmod);
    thunk_pool_release(g_thunk_i32_i32, PM_THUNK_I32_I32_POOL, wmod);
    thunk_pool_release(g_thunk_i32x2_i32, PM_THUNK_I32X2_I32_POOL, wmod);
    thunk_pool_release(g_thunk_i32x3_i32, PM_THUNK_I32X3_I32_POOL, wmod);
    thunk_pool_release(g_thunk_i64_i64, PM_THUNK_I64_I64_POOL, wmod);
    thunk_pool_release(g_thunk_f32_f32, PM_THUNK_F32_F32_POOL, wmod);
    thunk_pool_release(g_thunk_f64_f64, PM_THUNK_F64_F64_POOL, wmod);
    thunk_pool_release(g_thunk_f64x3_f64, PM_THUNK_F64X3_F64_POOL, wmod);
}

#else /* !MICROPY_PY_WASM */

void pm_mod_thunk_export(mp_pack_t *wmod, const char *pm_module_name,
    const char *wasm_export_name, const char *pm_func_name) {
    (void)wmod;
    (void)pm_module_name;
    (void)wasm_export_name;
    (void)pm_func_name;
}

void pm_mod_thunk_release_pack(mp_pack_t *wmod) {
    (void)wmod;
}

#endif /* MICROPY_PY_WASM */
