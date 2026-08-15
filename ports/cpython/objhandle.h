/*
 * Host obj-handle table — CPython Py_INCREF'd PyObject * slots.
 * Handles are 1-based; 0 is invalid. Twin of ports/micropython/objhandle.
 */
#ifndef PM_WASMMOD_PORTS_CPY_OBJHANDLE_H
#define PM_WASMMOD_PORTS_CPY_OBJHANDLE_H

#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif
#include <Python.h>

#include "pymergetic/wasmmod/host/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_WASMMOD_OBJ_HANDLE_SLOTS 32

pm_wasmmod_obj_handle_t pm_wasmmod_obj_handle_put(PyObject *obj);
PyObject *pm_wasmmod_obj_handle_get(pm_wasmmod_obj_handle_t handle);
void pm_wasmmod_obj_handle_release(pm_wasmmod_obj_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_CPY_OBJHANDLE_H */
