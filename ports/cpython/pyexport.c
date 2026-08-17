#define PY_SSIZE_T_CLEAN
#include <Python.h>

/*
 * CPython twin of src/pymergetic/wasmmod/pyexport/__impl__.c
 * Compile with -DPM_WASMMOD_CPYTHON=1 (do not also compile µPy __impl__.c).
 */
#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif

#include "pymergetic/wasmmod/pyexport.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/common/memcookie.h"
#include "ports/cpython/objhandle.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

typedef struct {
    bool used;
} pm_pyexport_slot_t;

static int32_t call_i32(PyObject *cb, PyObject *args) {
    PyObject *res = PyObject_CallObject(cb, args);
    if (res == NULL) {
        PyErr_Clear();
        return 0;
    }
    long v = PyLong_AsLong(res);
    Py_DECREF(res);
    if (v == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        return 0;
    }
    return (int32_t)v;
}

static int64_t call_i64(PyObject *cb, PyObject *args) {
    PyObject *res = PyObject_CallObject(cb, args);
    if (res == NULL) {
        PyErr_Clear();
        return 0;
    }
    long long v = PyLong_AsLongLong(res);
    Py_DECREF(res);
    if (v == -1 && PyErr_Occurred()) {
        PyErr_Clear();
        return 0;
    }
    return (int64_t)v;
}

static double call_f(PyObject *cb, PyObject *args) {
    PyObject *res = PyObject_CallObject(cb, args);
    if (res == NULL) {
        PyErr_Clear();
        return 0.0;
    }
    double v = PyFloat_AsDouble(res);
    Py_DECREF(res);
    if (v == -1.0 && PyErr_Occurred()) {
        PyErr_Clear();
        return 0.0;
    }
    return v;
}

static void *pyexport_acquire(pm_pyexport_slot_t *pool, PyObject **callables, void *const *ptrs,
    uint32_t n, PyObject *callable) {
    for (uint32_t i = 0; i < n; i++) {
        if (!pool[i].used) {
            Py_INCREF(callable);
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


#define PM_PYEXPORT_V_POOL 8
static pm_pyexport_slot_t g_pyexport_v[PM_PYEXPORT_V_POOL];
static PyObject *g_pyexport_v_callables[PM_PYEXPORT_V_POOL];

static int32_t pm_wasmmod_pyexport_v_0(void) {
    if (!g_pyexport_v[0].used) return 0;
    return call_i32(g_pyexport_v_callables[0], NULL);
}

static int32_t pm_wasmmod_pyexport_v_1(void) {
    if (!g_pyexport_v[1].used) return 0;
    return call_i32(g_pyexport_v_callables[1], NULL);
}

static int32_t pm_wasmmod_pyexport_v_2(void) {
    if (!g_pyexport_v[2].used) return 0;
    return call_i32(g_pyexport_v_callables[2], NULL);
}

static int32_t pm_wasmmod_pyexport_v_3(void) {
    if (!g_pyexport_v[3].used) return 0;
    return call_i32(g_pyexport_v_callables[3], NULL);
}

static int32_t pm_wasmmod_pyexport_v_4(void) {
    if (!g_pyexport_v[4].used) return 0;
    return call_i32(g_pyexport_v_callables[4], NULL);
}

static int32_t pm_wasmmod_pyexport_v_5(void) {
    if (!g_pyexport_v[5].used) return 0;
    return call_i32(g_pyexport_v_callables[5], NULL);
}

static int32_t pm_wasmmod_pyexport_v_6(void) {
    if (!g_pyexport_v[6].used) return 0;
    return call_i32(g_pyexport_v_callables[6], NULL);
}

static int32_t pm_wasmmod_pyexport_v_7(void) {
    if (!g_pyexport_v[7].used) return 0;
    return call_i32(g_pyexport_v_callables[7], NULL);
}
static void *const g_pyexport_v_ptrs[PM_PYEXPORT_V_POOL] = {
(void *)pm_wasmmod_pyexport_v_0, (void *)pm_wasmmod_pyexport_v_1, (void *)pm_wasmmod_pyexport_v_2, (void *)pm_wasmmod_pyexport_v_3, (void *)pm_wasmmod_pyexport_v_4, (void *)pm_wasmmod_pyexport_v_5, (void *)pm_wasmmod_pyexport_v_6, (void *)pm_wasmmod_pyexport_v_7
};

#define PM_PYEXPORT_1_POOL 8
static pm_pyexport_slot_t g_pyexport_1[PM_PYEXPORT_1_POOL];
static PyObject *g_pyexport_1_callables[PM_PYEXPORT_1_POOL];

static int32_t pm_wasmmod_pyexport_1_0(int32_t a0) {
    if (!g_pyexport_1[0].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[0], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_1_1(int32_t a0) {
    if (!g_pyexport_1[1].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[1], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_1_2(int32_t a0) {
    if (!g_pyexport_1[2].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[2], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_1_3(int32_t a0) {
    if (!g_pyexport_1[3].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[3], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_1_4(int32_t a0) {
    if (!g_pyexport_1[4].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[4], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_1_5(int32_t a0) {
    if (!g_pyexport_1[5].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[5], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_1_6(int32_t a0) {
    if (!g_pyexport_1[6].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[6], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_1_7(int32_t a0) {
    if (!g_pyexport_1[7].used) return 0;
    PyObject *args = Py_BuildValue("(i)", (int)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_1_callables[7], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_1_ptrs[PM_PYEXPORT_1_POOL] = {
(void *)pm_wasmmod_pyexport_1_0, (void *)pm_wasmmod_pyexport_1_1, (void *)pm_wasmmod_pyexport_1_2, (void *)pm_wasmmod_pyexport_1_3, (void *)pm_wasmmod_pyexport_1_4, (void *)pm_wasmmod_pyexport_1_5, (void *)pm_wasmmod_pyexport_1_6, (void *)pm_wasmmod_pyexport_1_7
};

#define PM_PYEXPORT_2_POOL 4
static pm_pyexport_slot_t g_pyexport_2[PM_PYEXPORT_2_POOL];
static PyObject *g_pyexport_2_callables[PM_PYEXPORT_2_POOL];

static int32_t pm_wasmmod_pyexport_2_0(int32_t a0, int32_t a1) {
    if (!g_pyexport_2[0].used) return 0;
    PyObject *args = Py_BuildValue("(ii)", (int)a0, (int)a1);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_2_callables[0], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_2_1(int32_t a0, int32_t a1) {
    if (!g_pyexport_2[1].used) return 0;
    PyObject *args = Py_BuildValue("(ii)", (int)a0, (int)a1);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_2_callables[1], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_2_2(int32_t a0, int32_t a1) {
    if (!g_pyexport_2[2].used) return 0;
    PyObject *args = Py_BuildValue("(ii)", (int)a0, (int)a1);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_2_callables[2], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_2_3(int32_t a0, int32_t a1) {
    if (!g_pyexport_2[3].used) return 0;
    PyObject *args = Py_BuildValue("(ii)", (int)a0, (int)a1);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_2_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_2_ptrs[PM_PYEXPORT_2_POOL] = {
(void *)pm_wasmmod_pyexport_2_0, (void *)pm_wasmmod_pyexport_2_1, (void *)pm_wasmmod_pyexport_2_2, (void *)pm_wasmmod_pyexport_2_3
};

#define PM_PYEXPORT_3_POOL 4
static pm_pyexport_slot_t g_pyexport_3[PM_PYEXPORT_3_POOL];
static PyObject *g_pyexport_3_callables[PM_PYEXPORT_3_POOL];

static int32_t pm_wasmmod_pyexport_3_0(int32_t a0, int32_t a1, int32_t a2) {
    if (!g_pyexport_3[0].used) return 0;
    PyObject *args = Py_BuildValue("(iii)", (int)a0, (int)a1, (int)a2);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_3_callables[0], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_3_1(int32_t a0, int32_t a1, int32_t a2) {
    if (!g_pyexport_3[1].used) return 0;
    PyObject *args = Py_BuildValue("(iii)", (int)a0, (int)a1, (int)a2);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_3_callables[1], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_3_2(int32_t a0, int32_t a1, int32_t a2) {
    if (!g_pyexport_3[2].used) return 0;
    PyObject *args = Py_BuildValue("(iii)", (int)a0, (int)a1, (int)a2);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_3_callables[2], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_3_3(int32_t a0, int32_t a1, int32_t a2) {
    if (!g_pyexport_3[3].used) return 0;
    PyObject *args = Py_BuildValue("(iii)", (int)a0, (int)a1, (int)a2);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int32_t out = call_i32(g_pyexport_3_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_3_ptrs[PM_PYEXPORT_3_POOL] = {
(void *)pm_wasmmod_pyexport_3_0, (void *)pm_wasmmod_pyexport_3_1, (void *)pm_wasmmod_pyexport_3_2, (void *)pm_wasmmod_pyexport_3_3
};

#define PM_PYEXPORT_I64_POOL 4
static pm_pyexport_slot_t g_pyexport_i64[PM_PYEXPORT_I64_POOL];
static PyObject *g_pyexport_i64_callables[PM_PYEXPORT_I64_POOL];

static int64_t pm_wasmmod_pyexport_i64_0(int64_t a0) {
    if (!g_pyexport_i64[0].used) return 0;
    PyObject *args = Py_BuildValue("(L)", (long long)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int64_t out = call_i64(g_pyexport_i64_callables[0], args);
    Py_DECREF(args);
    return out;
}

static int64_t pm_wasmmod_pyexport_i64_1(int64_t a0) {
    if (!g_pyexport_i64[1].used) return 0;
    PyObject *args = Py_BuildValue("(L)", (long long)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int64_t out = call_i64(g_pyexport_i64_callables[1], args);
    Py_DECREF(args);
    return out;
}

static int64_t pm_wasmmod_pyexport_i64_2(int64_t a0) {
    if (!g_pyexport_i64[2].used) return 0;
    PyObject *args = Py_BuildValue("(L)", (long long)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int64_t out = call_i64(g_pyexport_i64_callables[2], args);
    Py_DECREF(args);
    return out;
}

static int64_t pm_wasmmod_pyexport_i64_3(int64_t a0) {
    if (!g_pyexport_i64[3].used) return 0;
    PyObject *args = Py_BuildValue("(L)", (long long)a0);
    if (args == NULL) { PyErr_Clear(); return 0; }
    int64_t out = call_i64(g_pyexport_i64_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_i64_ptrs[PM_PYEXPORT_I64_POOL] = {
(void *)pm_wasmmod_pyexport_i64_0, (void *)pm_wasmmod_pyexport_i64_1, (void *)pm_wasmmod_pyexport_i64_2, (void *)pm_wasmmod_pyexport_i64_3
};

#define PM_PYEXPORT_F32_POOL 4
static pm_pyexport_slot_t g_pyexport_f32[PM_PYEXPORT_F32_POOL];
static PyObject *g_pyexport_f32_callables[PM_PYEXPORT_F32_POOL];

static float pm_wasmmod_pyexport_f32_0(float a0) {
    if (!g_pyexport_f32[0].used) return 0.f;
    PyObject *args = Py_BuildValue("(f)", (double)a0);
    if (args == NULL) { PyErr_Clear(); return 0.f; }
    float out = (float)call_f(g_pyexport_f32_callables[0], args);
    Py_DECREF(args);
    return out;
}

static float pm_wasmmod_pyexport_f32_1(float a0) {
    if (!g_pyexport_f32[1].used) return 0.f;
    PyObject *args = Py_BuildValue("(f)", (double)a0);
    if (args == NULL) { PyErr_Clear(); return 0.f; }
    float out = (float)call_f(g_pyexport_f32_callables[1], args);
    Py_DECREF(args);
    return out;
}

static float pm_wasmmod_pyexport_f32_2(float a0) {
    if (!g_pyexport_f32[2].used) return 0.f;
    PyObject *args = Py_BuildValue("(f)", (double)a0);
    if (args == NULL) { PyErr_Clear(); return 0.f; }
    float out = (float)call_f(g_pyexport_f32_callables[2], args);
    Py_DECREF(args);
    return out;
}

static float pm_wasmmod_pyexport_f32_3(float a0) {
    if (!g_pyexport_f32[3].used) return 0.f;
    PyObject *args = Py_BuildValue("(f)", (double)a0);
    if (args == NULL) { PyErr_Clear(); return 0.f; }
    float out = (float)call_f(g_pyexport_f32_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_f32_ptrs[PM_PYEXPORT_F32_POOL] = {
(void *)pm_wasmmod_pyexport_f32_0, (void *)pm_wasmmod_pyexport_f32_1, (void *)pm_wasmmod_pyexport_f32_2, (void *)pm_wasmmod_pyexport_f32_3
};

#define PM_PYEXPORT_F64_POOL 4
static pm_pyexport_slot_t g_pyexport_f64[PM_PYEXPORT_F64_POOL];
static PyObject *g_pyexport_f64_callables[PM_PYEXPORT_F64_POOL];

static double pm_wasmmod_pyexport_f64_0(double a0) {
    if (!g_pyexport_f64[0].used) return 0.0;
    PyObject *args = Py_BuildValue("(d)", a0);
    if (args == NULL) { PyErr_Clear(); return 0.0; }
    double out = call_f(g_pyexport_f64_callables[0], args);
    Py_DECREF(args);
    return out;
}

static double pm_wasmmod_pyexport_f64_1(double a0) {
    if (!g_pyexport_f64[1].used) return 0.0;
    PyObject *args = Py_BuildValue("(d)", a0);
    if (args == NULL) { PyErr_Clear(); return 0.0; }
    double out = call_f(g_pyexport_f64_callables[1], args);
    Py_DECREF(args);
    return out;
}

static double pm_wasmmod_pyexport_f64_2(double a0) {
    if (!g_pyexport_f64[2].used) return 0.0;
    PyObject *args = Py_BuildValue("(d)", a0);
    if (args == NULL) { PyErr_Clear(); return 0.0; }
    double out = call_f(g_pyexport_f64_callables[2], args);
    Py_DECREF(args);
    return out;
}

static double pm_wasmmod_pyexport_f64_3(double a0) {
    if (!g_pyexport_f64[3].used) return 0.0;
    PyObject *args = Py_BuildValue("(d)", a0);
    if (args == NULL) { PyErr_Clear(); return 0.0; }
    double out = call_f(g_pyexport_f64_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_f64_ptrs[PM_PYEXPORT_F64_POOL] = {
(void *)pm_wasmmod_pyexport_f64_0, (void *)pm_wasmmod_pyexport_f64_1, (void *)pm_wasmmod_pyexport_f64_2, (void *)pm_wasmmod_pyexport_f64_3
};

#define PM_PYEXPORT_BUFPTR_POOL 4
static pm_pyexport_slot_t g_pyexport_bufptr[PM_PYEXPORT_BUFPTR_POOL];
static PyObject *g_pyexport_bufptr_callables[PM_PYEXPORT_BUFPTR_POOL];

static int32_t pm_wasmmod_pyexport_bufptr_0(const uint8_t *ptr, uint32_t len) {
    if (!g_pyexport_bufptr[0].used || (len > 0 && ptr == NULL)) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_bufptr_callables[0], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_bufptr_1(const uint8_t *ptr, uint32_t len) {
    if (!g_pyexport_bufptr[1].used || (len > 0 && ptr == NULL)) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_bufptr_callables[1], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_bufptr_2(const uint8_t *ptr, uint32_t len) {
    if (!g_pyexport_bufptr[2].used || (len > 0 && ptr == NULL)) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_bufptr_callables[2], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_bufptr_3(const uint8_t *ptr, uint32_t len) {
    if (!g_pyexport_bufptr[3].used || (len > 0 && ptr == NULL)) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_bufptr_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_bufptr_ptrs[PM_PYEXPORT_BUFPTR_POOL] = {
(void *)pm_wasmmod_pyexport_bufptr_0, (void *)pm_wasmmod_pyexport_bufptr_1, (void *)pm_wasmmod_pyexport_bufptr_2, (void *)pm_wasmmod_pyexport_bufptr_3
};

#define PM_PYEXPORT_MEM_POOL 4
static pm_pyexport_slot_t g_pyexport_mem[PM_PYEXPORT_MEM_POOL];
static PyObject *g_pyexport_mem_callables[PM_PYEXPORT_MEM_POOL];

static int32_t pm_wasmmod_pyexport_mem_0(pm_wasmmod_mem_cookie_t cookie) {
    if (!g_pyexport_mem[0].used) return -1;
    const uint8_t *ptr = NULL;
    uint32_t len = 0;
    if (pm_wasmmod_mem_cookie_get(cookie, &ptr, &len) != 0) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_mem_callables[0], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_mem_1(pm_wasmmod_mem_cookie_t cookie) {
    if (!g_pyexport_mem[1].used) return -1;
    const uint8_t *ptr = NULL;
    uint32_t len = 0;
    if (pm_wasmmod_mem_cookie_get(cookie, &ptr, &len) != 0) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_mem_callables[1], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_mem_2(pm_wasmmod_mem_cookie_t cookie) {
    if (!g_pyexport_mem[2].used) return -1;
    const uint8_t *ptr = NULL;
    uint32_t len = 0;
    if (pm_wasmmod_mem_cookie_get(cookie, &ptr, &len) != 0) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_mem_callables[2], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_mem_3(pm_wasmmod_mem_cookie_t cookie) {
    if (!g_pyexport_mem[3].used) return -1;
    const uint8_t *ptr = NULL;
    uint32_t len = 0;
    if (pm_wasmmod_mem_cookie_get(cookie, &ptr, &len) != 0) return -1;
    PyObject *buf = PyBytes_FromStringAndSize(
        ptr == NULL ? "" : (const char *)ptr, (Py_ssize_t)len);
    if (buf == NULL) { PyErr_Clear(); return -1; }
    PyObject *args = PyTuple_Pack(1, buf);
    Py_DECREF(buf);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_mem_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_mem_ptrs[PM_PYEXPORT_MEM_POOL] = {
(void *)pm_wasmmod_pyexport_mem_0, (void *)pm_wasmmod_pyexport_mem_1, (void *)pm_wasmmod_pyexport_mem_2, (void *)pm_wasmmod_pyexport_mem_3
};

#define PM_PYEXPORT_OBJ_POOL 4
static pm_pyexport_slot_t g_pyexport_obj[PM_PYEXPORT_OBJ_POOL];
static PyObject *g_pyexport_obj_callables[PM_PYEXPORT_OBJ_POOL];

static int32_t pm_wasmmod_pyexport_obj_0(pm_wasmmod_obj_handle_t handle) {
    if (!g_pyexport_obj[0].used) return -1;
    PyObject *arg = pm_wasmmod_obj_handle_get(handle);
    if (arg == NULL) return -1;
    PyObject *args = PyTuple_Pack(1, arg);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_obj_callables[0], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_obj_1(pm_wasmmod_obj_handle_t handle) {
    if (!g_pyexport_obj[1].used) return -1;
    PyObject *arg = pm_wasmmod_obj_handle_get(handle);
    if (arg == NULL) return -1;
    PyObject *args = PyTuple_Pack(1, arg);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_obj_callables[1], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_obj_2(pm_wasmmod_obj_handle_t handle) {
    if (!g_pyexport_obj[2].used) return -1;
    PyObject *arg = pm_wasmmod_obj_handle_get(handle);
    if (arg == NULL) return -1;
    PyObject *args = PyTuple_Pack(1, arg);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_obj_callables[2], args);
    Py_DECREF(args);
    return out;
}

static int32_t pm_wasmmod_pyexport_obj_3(pm_wasmmod_obj_handle_t handle) {
    if (!g_pyexport_obj[3].used) return -1;
    PyObject *arg = pm_wasmmod_obj_handle_get(handle);
    if (arg == NULL) return -1;
    PyObject *args = PyTuple_Pack(1, arg);
    if (args == NULL) { PyErr_Clear(); return -1; }
    int32_t out = call_i32(g_pyexport_obj_callables[3], args);
    Py_DECREF(args);
    return out;
}
static void *const g_pyexport_obj_ptrs[PM_PYEXPORT_OBJ_POOL] = {
(void *)pm_wasmmod_pyexport_obj_0, (void *)pm_wasmmod_pyexport_obj_1, (void *)pm_wasmmod_pyexport_obj_2, (void *)pm_wasmmod_pyexport_obj_3
};

int pm_wasmmod_pyexport_export_py(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable, uint32_t nargs) {
    if (fqn == NULL || export_name == NULL || callable == NULL || !PyCallable_Check(callable)) {
        return -1;
    }
    void *fn = NULL;
    const char *sig = NULL;
    switch (nargs) {
        case 0:
            fn = pyexport_acquire(g_pyexport_v, g_pyexport_v_callables, g_pyexport_v_ptrs,
                PM_PYEXPORT_V_POOL, callable);
            sig = "int32_t(void)";
            break;
        case 1:
            fn = pyexport_acquire(g_pyexport_1, g_pyexport_1_callables, g_pyexport_1_ptrs,
                PM_PYEXPORT_1_POOL, callable);
            sig = "int32_t(int32_t)";
            break;
        case 2:
            fn = pyexport_acquire(g_pyexport_2, g_pyexport_2_callables, g_pyexport_2_ptrs,
                PM_PYEXPORT_2_POOL, callable);
            sig = "int32_t(int32_t, int32_t)";
            break;
        case 3:
            fn = pyexport_acquire(g_pyexport_3, g_pyexport_3_callables, g_pyexport_3_ptrs,
                PM_PYEXPORT_3_POOL, callable);
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

int pm_wasmmod_pyexport_export_py_i64(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == NULL || !PyCallable_Check(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_i64, g_pyexport_i64_callables, g_pyexport_i64_ptrs,
        PM_PYEXPORT_I64_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_I64, "int64_t(int64_t)");
}

int pm_wasmmod_pyexport_export_py_f32(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == NULL || !PyCallable_Check(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_f32, g_pyexport_f32_callables, g_pyexport_f32_ptrs,
        PM_PYEXPORT_F32_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_F32, "float(float)");
}

int pm_wasmmod_pyexport_export_py_f64(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == NULL || !PyCallable_Check(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_f64, g_pyexport_f64_callables, g_pyexport_f64_ptrs,
        PM_PYEXPORT_F64_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_F64, "double(double)");
}

int pm_wasmmod_pyexport_export_py_bufptr(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == NULL || !PyCallable_Check(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_bufptr, g_pyexport_bufptr_callables, g_pyexport_bufptr_ptrs,
        PM_PYEXPORT_BUFPTR_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_BUFPTR,
        "int32_t(const uint8_t *, uint32_t)");
}

int pm_wasmmod_pyexport_export_py_mem(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == NULL || !PyCallable_Check(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_mem, g_pyexport_mem_callables, g_pyexport_mem_ptrs,
        PM_PYEXPORT_MEM_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_MEM,
        "int32_t(pm_wasmmod_mem_cookie_t)");
}

int pm_wasmmod_pyexport_export_py_obj(const char *fqn, const char *export_name,
    pm_wasmmod_py_obj_t callable) {
    if (fqn == NULL || export_name == NULL || callable == NULL || !PyCallable_Check(callable)) {
        return -1;
    }
    void *fn = pyexport_acquire(g_pyexport_obj, g_pyexport_obj_callables, g_pyexport_obj_ptrs,
        PM_PYEXPORT_OBJ_POOL, callable);
    return pyexport_publish(fqn, export_name, fn, PM_WASMMOD_REGISTRY_EXPORT_OBJ,
        "int32_t(pm_wasmmod_obj_handle_t)");
}

static int pyexport_bind_one_sig(const char *fqn, const char *export_name, const char *sig,
    PyObject *callable) {
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

static int fqn_to_c_prefix(const char *fqn, char *out, size_t out_sz) {
    if (fqn == NULL || out == NULL || out_sz < 8) {
        return -1;
    }
    const char *rest = fqn;
    if (strncmp(fqn, "pymergetic.", 11) == 0) {
        rest = fqn + 11;
    }
    size_t o = 0;
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

static PyObject *pyexport_lookup_attr(PyObject *module, const char *fqn, const char *export_name) {
    char prefix[160];
    if (fqn_to_c_prefix(fqn, prefix, sizeof(prefix)) != 0) {
        return NULL;
    }
    size_t plen = strlen(prefix);
    if (strncmp(export_name, prefix, plen) != 0) {
        return NULL;
    }
    const char *py_name = export_name + plen;
    if (py_name[0] == '\0') {
        return NULL;
    }
    return PyObject_GetAttrString(module, py_name);
}

static int pyexport_bind_named(const char *fqn, PyObject *module, const char *export_name,
    const char *sig) {
    PyObject *attr = pyexport_lookup_attr(module, fqn, export_name);
    if (attr == NULL) {
        PyErr_Clear();
        return -1;
    }
    if (!PyCallable_Check(attr)) {
        Py_DECREF(attr);
        return -1;
    }
    int st = pyexport_bind_one_sig(fqn, export_name, sig, attr);
    Py_DECREF(attr);
    return st;
}

static int pyexport_bind_from_registry(const char *fqn, PyObject *module) {
    size_t flen = strlen(fqn);
    uint32_t n = pm_wasmmod_registry_export_count((const uint8_t *)fqn, (uint32_t)flen);
    int bound = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t name[128];
        uint32_t name_len = sizeof(name);
        uint8_t sig[128];
        uint32_t sig_len = sizeof(sig);
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (pm_wasmmod_registry_export_at((const uint8_t *)fqn, (uint32_t)flen, i, name, &name_len,
                &kind, sig, &sig_len) == 0) {
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

static int pyexport_bind_from_exports_h(const char *fqn, PyObject *module, const char *path) {
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
        char *args_start = paren + 1;
        char *args_end = semi;
        while (args_end > args_start && args_end[-1] != ')') {
            args_end--;
        }
        if (args_end > args_start && args_end[-1] == ')') {
            args_end--;
        }
        *args_end = '\0';
        char *name_end = paren;
        while (name_end > name_start && (name_end[-1] == ' ' || name_end[-1] == '\t')) {
            name_end--;
        }
        *name_end = '\0';
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

static int pyexport_try_exports_h(const char *fqn, PyObject *module) {
    PyObject *file_obj = PyObject_GetAttrString(module, "__file__");
    if (file_obj == NULL) {
        PyErr_Clear();
        return 0;
    }
    if (!PyUnicode_Check(file_obj)) {
        Py_DECREF(file_obj);
        return 0;
    }
    const char *file = PyUnicode_AsUTF8(file_obj);
    if (file == NULL) {
        Py_DECREF(file_obj);
        return 0;
    }
    size_t n = strlen(file);
    char path[512];
    if (n + 16 >= sizeof(path)) {
        Py_DECREF(file_obj);
        return 0;
    }
    memcpy(path, file, n + 1);
    Py_DECREF(file_obj);
    char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return 0;
    }
    strcpy(slash + 1, "__exports__.h");
    return pyexport_bind_from_exports_h(fqn, module, path);
}

int pm_wasmmod_pyexport_bind_module(const char *fqn, pm_wasmmod_py_obj_t module) {
    if (fqn == NULL || module == NULL) {
        return -1;
    }
    int bound = pyexport_bind_from_registry(fqn, module);
    if (bound > 0) {
        return bound;
    }
    return pyexport_try_exports_h(fqn, module);
}
