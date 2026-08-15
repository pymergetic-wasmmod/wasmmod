/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Live Python → typed C-ABI thunks for `impl = "py"` access faces.
 * Facegen emits decls (`pm_util_pysample_hello`, …); these pools install the
 * real function pointers into the registry so resolve / CONNECT call Python.
 *
 * Bounded shapes (matches util.gen discover): 0..3×i32→i32, i64/f32/f64
 * one-arg, bufptr `(const uint8_t *, uint32_t) → i32`, cookie mem
 * `int32_t(pm_wasmmod_mem_cookie_t)`, handle obj
 * `int32_t(pm_wasmmod_obj_handle_t)`.
 *
 * Object type is portable (`pm_wasmmod_py_obj_t`): µPy `mp_obj_t` or
 * CPython `PyObject *` depending on the port define.
 */

#ifndef PYMERGETIC_WASMMOD_PYEXPORT_EXPORT_H
#define PYMERGETIC_WASMMOD_PYEXPORT_EXPORT_H

#include <stdint.h>

#include "pymergetic/wasmmod/host/pyobj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Publish/overwrite one registry export with a typed trampoline that calls
 * `callable`. `export_name` is the C ABI name (e.g. pm_util_pysample_hello).
 * Returns 0 on success, -1 on bad args / unsupported shape / pool full. */
int pm_wasmmod_pyexport_export_py(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable, uint32_t nargs);

int pm_wasmmod_pyexport_export_py_i64(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable);
int pm_wasmmod_pyexport_export_py_f32(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable);
int pm_wasmmod_pyexport_export_py_f64(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable);

/* ELF/native: wrap [ptr,len) as bytes, call callable(buf) → int. */
int pm_wasmmod_pyexport_export_py_bufptr(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable);

/* Cookie mem: lookup host cookie → bytes, call callable(buf) → int. */
int pm_wasmmod_pyexport_export_py_mem(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable);

/* Handle obj: lookup host handle → object, call callable(obj) → int. */
int pm_wasmmod_pyexport_export_py_obj(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable);

/* Bind every public callable on `module` that matches a known face:
 *   1) registry exports for `fqn` (sig from export_at), else
 *   2) sibling `__exports__.h` beside module.__file__ (parse prototypes).
 * Strips `pm_<path>_` prefix to resolve the Python attribute.
 * Returns number of exports bound, or -1 on hard failure. */
int pm_wasmmod_pyexport_bind_module(const char *fqn, pm_wasmmod_py_obj_t module);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_PYEXPORT_EXPORT_H */
