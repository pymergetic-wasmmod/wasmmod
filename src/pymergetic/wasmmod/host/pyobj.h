/*
 * Portable Python object pointer for pyexport faces (µPy or CPython).
 * Ports define MICROPY_PY_WASM or PM_WASMMOD_CPYTHON before including.
 * CPython wins if both are set (verify/pack host TUs may define MICROPY_PY_WASM).
 */
#ifndef PYMERGETIC_WASMMOD_HOST_PYOBJ_H
#define PYMERGETIC_WASMMOD_HOST_PYOBJ_H

#if defined(PM_WASMMOD_CPYTHON) && PM_WASMMOD_CPYTHON
#include <Python.h>
typedef PyObject *pm_wasmmod_py_obj_t;
#ifndef PM_WASMMOD_PY_OBJ_NULL
#define PM_WASMMOD_PY_OBJ_NULL NULL
#endif
#elif defined(MICROPY_PY_WASM) && MICROPY_PY_WASM
#include "py/obj.h"
typedef mp_obj_t pm_wasmmod_py_obj_t;
#ifndef PM_WASMMOD_PY_OBJ_NULL
#define PM_WASMMOD_PY_OBJ_NULL MP_OBJ_NULL
#endif
#else
typedef void *pm_wasmmod_py_obj_t;
#ifndef PM_WASMMOD_PY_OBJ_NULL
#define PM_WASMMOD_PY_OBJ_NULL ((pm_wasmmod_py_obj_t)0)
#endif
#endif

#endif /* PYMERGETIC_WASMMOD_HOST_PYOBJ_H */
