#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "ports/cpython/objhandle.h"

#include <stdint.h>

typedef struct {
    int used;
} pm_wasmmod_obj_slot_meta_t;

static pm_wasmmod_obj_slot_meta_t g_obj_meta[PM_WASMMOD_OBJ_HANDLE_SLOTS];
static PyObject *g_obj_handles[PM_WASMMOD_OBJ_HANDLE_SLOTS];

pm_wasmmod_obj_handle_t pm_wasmmod_obj_handle_put(PyObject *obj) {
    if (obj == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < PM_WASMMOD_OBJ_HANDLE_SLOTS; i++) {
        if (!g_obj_meta[i].used) {
            Py_INCREF(obj);
            g_obj_handles[i] = obj;
            g_obj_meta[i].used = 1;
            return (pm_wasmmod_obj_handle_t)(i + 1);
        }
    }
    return 0;
}

PyObject *pm_wasmmod_obj_handle_get(pm_wasmmod_obj_handle_t handle) {
    if (handle <= 0 || (uint32_t)handle > PM_WASMMOD_OBJ_HANDLE_SLOTS) {
        return NULL;
    }
    uint32_t i = (uint32_t)handle - 1;
    if (!g_obj_meta[i].used) {
        return NULL;
    }
    return g_obj_handles[i];
}

void pm_wasmmod_obj_handle_release(pm_wasmmod_obj_handle_t handle) {
    if (handle <= 0 || (uint32_t)handle > PM_WASMMOD_OBJ_HANDLE_SLOTS) {
        return;
    }
    uint32_t i = (uint32_t)handle - 1;
    if (!g_obj_meta[i].used) {
        return;
    }
    Py_CLEAR(g_obj_handles[i]);
    g_obj_meta[i].used = 0;
}
