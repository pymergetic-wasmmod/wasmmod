/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/call.h"
#include "pm_common.h"
#include "host.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <stdint.h>
#include <string.h>

pm_upy_obj_t pm_upy_call_function_0(pm_upy_obj_t fun) {
    return (pm_upy_obj_t)(uintptr_t)mp_call_function_0((mp_obj_t)(uintptr_t)fun);
}

pm_upy_obj_t pm_upy_call_function_1(pm_upy_obj_t fun, pm_upy_obj_t arg) {
    return (pm_upy_obj_t)(uintptr_t)mp_call_function_1(
        (mp_obj_t)(uintptr_t)fun, (mp_obj_t)(uintptr_t)arg);
}

pm_upy_obj_t pm_upy_call_function_n(pm_upy_obj_t fun, size_t n, pm_upy_obj_t *args) {
    return (pm_upy_obj_t)(uintptr_t)mp_call_function_n_kw(
        (mp_obj_t)(uintptr_t)fun, n, 0, (const mp_obj_t *)args);
}

pm_upy_obj_t pm_upy_call_method(pm_upy_obj_t obj, const char *name, size_t n, pm_upy_obj_t *args) {
    if (!name) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    mp_obj_t meth = mp_load_attr((mp_obj_t)(uintptr_t)obj, qstr_from_str(name));
    return (pm_upy_obj_t)(uintptr_t)mp_call_function_n_kw(meth, n, 0, (const mp_obj_t *)args);
}

pm_upy_obj_t pm_upy_fn_call_async(pm_upy_obj_t fun, size_t n, pm_upy_obj_t *args) {
    /* Async scheduling is host-specific; call sync for now. */
    return pm_upy_call_function_n(fun, n, args);
}

uint32_t pm_upy_fn_resolve(const char *dotted) {
    if (!dotted || !dotted[0]) {
        return 0;
    }
    /* Freestanding hosts may lack strrchr — scan manually. */
    const char *last_dot = NULL;
    for (const char *p = dotted; *p; p++) {
        if (*p == '.') {
            last_dot = p;
        }
    }
    if (!last_dot || last_dot == dotted || last_dot[1] == '\0') {
        return 0;
    }

    size_t mod_len = (size_t)(last_dot - dotted);
    char mod_buf[160];
    if (mod_len >= sizeof(mod_buf)) {
        return 0;
    }
    for (size_t i = 0; i < mod_len; i++) {
        mod_buf[i] = dotted[i];
    }
    mod_buf[mod_len] = '\0';
    const char *attr = last_dot + 1;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        /* Non-empty fromlist → leaf module (µPy import semantics). */
        mp_obj_t mod = mp_import_name(
            qstr_from_str(mod_buf), mp_const_true, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t fun = mp_load_attr(mod, qstr_from_str(attr));
        if (!mp_obj_is_callable(fun)) {
            nlr_pop();
            return 0;
        }
        int32_t h = mp_wasm_handle_register(fun);
        nlr_pop();
        return h > 0 ? (uint32_t)h : 0;
    }
    return 0;
}

static mp_obj_t pm_upy_fn_resolve_obj(uint32_t fn_h) {
    if (fn_h == 0 || fn_h > (uint32_t)INT32_MAX) {
        return mp_const_none;
    }
    return mp_wasm_handle_resolve((int32_t)fn_h);
}

pm_upy_obj_t pm_upy_fn_call(uint32_t fn_h, size_t n, pm_upy_obj_t *args) {
    mp_obj_t fun = pm_upy_fn_resolve_obj(fn_h);
    if (fun == mp_const_none) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_n_kw(
            fun, n, 0, (const mp_obj_t *)args);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)res;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}

int pm_upy_fn_call_i32(uint32_t fn_h, int32_t a, int32_t b, int32_t *out) {
    if (!out) {
        return PM_ERR_ARG;
    }
    mp_obj_t fun = pm_upy_fn_resolve_obj(fn_h);
    if (fun == mp_const_none) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args[2] = {
            mp_obj_new_int(a),
            mp_obj_new_int(b),
        };
        mp_obj_t res = mp_call_function_n_kw(fun, 2, 0, args);
        *out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
}
