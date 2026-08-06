/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/ops.h"
#include "pm_common.h"
#include "py/obj.h"
#include "py/runtime.h"

pm_upy_obj_t pm_upy_unary_op(int op, pm_upy_obj_t o) {
    return (pm_upy_obj_t)(uintptr_t)mp_unary_op((mp_unary_op_t)op, (mp_obj_t)(uintptr_t)o);
}

pm_upy_obj_t pm_upy_binary_op(int op, pm_upy_obj_t lhs, pm_upy_obj_t rhs) {
    return (pm_upy_obj_t)(uintptr_t)mp_binary_op(
        (mp_binary_op_t)op, (mp_obj_t)(uintptr_t)lhs, (mp_obj_t)(uintptr_t)rhs);
}

pm_upy_obj_t pm_upy_getiter(pm_upy_obj_t o) {
    return (pm_upy_obj_t)(uintptr_t)mp_getiter((mp_obj_t)(uintptr_t)o, NULL);
}

pm_upy_obj_t pm_upy_iternext(pm_upy_obj_t o) {
    return (pm_upy_obj_t)(uintptr_t)mp_iternext((mp_obj_t)(uintptr_t)o);
}

pm_upy_obj_t pm_upy_subscr(pm_upy_obj_t base, pm_upy_obj_t index, pm_upy_obj_t value) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_subscr(
        (mp_obj_t)(uintptr_t)base, (mp_obj_t)(uintptr_t)index, (mp_obj_t)(uintptr_t)value);
}

size_t pm_upy_len(pm_upy_obj_t o) {
    mp_obj_t len = mp_obj_len_maybe((mp_obj_t)(uintptr_t)o);
    if (len == MP_OBJ_NULL) {
        return 0;
    }
    return (size_t)mp_obj_get_int(len);
}

int pm_upy_equal(pm_upy_obj_t a, pm_upy_obj_t b) {
    return mp_obj_equal((mp_obj_t)(uintptr_t)a, (mp_obj_t)(uintptr_t)b) ? 1 : 0;
}

pm_upy_obj_t pm_upy_get_type(pm_upy_obj_t o) {
    return (pm_upy_obj_t)(uintptr_t)MP_OBJ_FROM_PTR(mp_obj_get_type((mp_obj_t)(uintptr_t)o));
}

int pm_upy_is_subclass(pm_upy_obj_t obj, pm_upy_obj_t classinfo) {
    /* Both args are type objects (issubclass semantics). */
    return mp_obj_is_subclass_fast(
               (mp_obj_t)(uintptr_t)obj, (mp_obj_t)(uintptr_t)classinfo)
        ? 1
        : 0;
}

pm_upy_obj_t pm_upy_load_global(const char *name) {
    if (!name) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_load_global(qstr_from_str(name));
}

int pm_upy_store_global(const char *name, pm_upy_obj_t val) {
    if (!name) {
        return PM_ERR_ARG;
    }
    mp_store_global(qstr_from_str(name), (mp_obj_t)(uintptr_t)val);
    return PM_OK;
}

pm_upy_obj_t pm_upy_load_name(const char *name) {
    if (!name) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_load_name(qstr_from_str(name));
}

int pm_upy_store_name(const char *name, pm_upy_obj_t val) {
    if (!name) {
        return PM_ERR_ARG;
    }
    mp_store_name(qstr_from_str(name), (mp_obj_t)(uintptr_t)val);
    return PM_OK;
}
