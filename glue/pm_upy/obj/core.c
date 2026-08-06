/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/core.h"
#include "pm_common.h"
#include "py/binary.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#ifndef MICROPY_PY_BUILTINS_BYTEARRAY
#define MICROPY_PY_BUILTINS_BYTEARRAY 0
#endif
#ifndef MICROPY_PY_BUILTINS_MEMORYVIEW
#define MICROPY_PY_BUILTINS_MEMORYVIEW 0
#endif
#ifndef MICROPY_PY_BUILTINS_COMPLEX
#define MICROPY_PY_BUILTINS_COMPLEX 0
#endif
#ifndef MICROPY_PY_BUILTINS_SET
#define MICROPY_PY_BUILTINS_SET 0
#endif
#ifndef MICROPY_PY_BUILTINS_SLICE
#define MICROPY_PY_BUILTINS_SLICE 0
#endif
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL 0
#endif

pm_upy_obj_t pm_upy_obj_none(void) {
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}

pm_upy_obj_t pm_upy_obj_new_int_from_ll(long long i) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_int_from_ll(i);
}

long long pm_upy_obj_get_ll(pm_upy_obj_t o) {
    return mp_obj_get_ll((mp_obj_t)(uintptr_t)o);
}

pm_upy_obj_t pm_upy_obj_new_str(const char *s, size_t len) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_str(s, len);
}

pm_upy_obj_t pm_upy_obj_new_bytes(const uint8_t *b, size_t len) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_bytes(b, len);
}

pm_upy_obj_t pm_upy_obj_new_int(intptr_t i) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_int((mp_int_t)i);
}

intptr_t pm_upy_obj_get_int(pm_upy_obj_t o) {
    return (intptr_t)mp_obj_get_int((mp_obj_t)(uintptr_t)o);
}

int pm_upy_obj_str_get(pm_upy_obj_t o, const char **data_out, size_t *len_out) {
    size_t len = 0;
    const char *s = mp_obj_str_get_data((mp_obj_t)(uintptr_t)o, &len);
    if (data_out) {
        *data_out = s;
    }
    if (len_out) {
        *len_out = len;
    }
    return s ? PM_OK : PM_ERR;
}

pm_upy_obj_t pm_upy_obj_new_bool(int v) {
    return (pm_upy_obj_t)(uintptr_t)(v ? mp_const_true : mp_const_false);
}

pm_upy_obj_t pm_upy_obj_new_float(double v) {
#if MICROPY_FLOAT_IMPL != MICROPY_FLOAT_IMPL_NONE
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_float((mp_float_t)v);
#else
    (void)v;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

double pm_upy_obj_get_float(pm_upy_obj_t o) {
#if MICROPY_FLOAT_IMPL != MICROPY_FLOAT_IMPL_NONE
    return (double)mp_obj_get_float((mp_obj_t)(uintptr_t)o);
#else
    (void)o;
    return 0.0;
#endif
}

pm_upy_obj_t pm_upy_obj_new_bytearray(size_t n, const uint8_t *data) {
#if MICROPY_PY_BUILTINS_BYTEARRAY
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_bytearray(n, data);
#else
    (void)n;
    (void)data;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

pm_upy_obj_t pm_upy_obj_new_memoryview(pm_upy_obj_t base) {
#if MICROPY_PY_BUILTINS_MEMORYVIEW
    mp_buffer_info_t bufinfo;
    if (!mp_get_buffer((mp_obj_t)(uintptr_t)base, &bufinfo, MP_BUFFER_READ)) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    size_t sz = mp_binary_get_size('@', bufinfo.typecode, NULL);
    size_t nitems = sz ? bufinfo.len / sz : 0;
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_memoryview(bufinfo.typecode, nitems, bufinfo.buf);
#else
    (void)base;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

pm_upy_obj_t pm_upy_obj_new_slice(pm_upy_obj_t start, pm_upy_obj_t stop, pm_upy_obj_t step) {
#if MICROPY_PY_BUILTINS_SLICE
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_slice(
        (mp_obj_t)(uintptr_t)start, (mp_obj_t)(uintptr_t)stop, (mp_obj_t)(uintptr_t)step);
#else
    (void)start;
    (void)stop;
    (void)step;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

pm_upy_obj_t pm_upy_obj_new_complex(double re, double im) {
#if MICROPY_PY_BUILTINS_COMPLEX
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_complex((mp_float_t)re, (mp_float_t)im);
#else
    (void)re;
    (void)im;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

pm_upy_obj_t pm_upy_obj_new_set(size_t n) {
#if MICROPY_PY_BUILTINS_SET
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_set(n, NULL);
#else
    (void)n;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

pm_upy_obj_t pm_upy_obj_to_str(pm_upy_obj_t o) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_str_make_new(&mp_type_str, 1, 0, (mp_obj_t[]){
        (mp_obj_t)(uintptr_t)o,
    });
}
