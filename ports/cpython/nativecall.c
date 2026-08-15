#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "ports/cpython/nativecall.h"

#include <string.h>

#include "ports/common/memcookie.h"
#include "ports/cpython/objhandle.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

static int fetch_sig(const char *fqn, const char *export_name, char *sig, size_t sig_sz) {
    size_t flen = strlen(fqn);
    uint32_t n = pm_wasmmod_registry_export_count((const uint8_t *)fqn, (uint32_t)flen);
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t name[128];
        uint32_t name_len = sizeof(name);
        uint8_t sbuf[160];
        uint32_t slen = sizeof(sbuf);
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (!pm_wasmmod_registry_export_at((const uint8_t *)fqn, (uint32_t)flen, i, name, &name_len,
                &kind, sbuf, &slen)) {
            continue;
        }
        name[name_len < sizeof(name) ? name_len : sizeof(name) - 1] = '\0';
        if (strcmp((const char *)name, export_name) != 0) {
            continue;
        }
        size_t copy = slen < sig_sz - 1 ? slen : sig_sz - 1;
        memcpy(sig, sbuf, copy);
        sig[copy] = '\0';
        return 0;
    }
    sig[0] = '\0';
    return -1;
}

/* Caller must PyBuffer_Release(view) after the native call returns. */
static int get_buffer(PyObject *obj, Py_buffer *view) {
    return PyObject_GetBuffer(obj, view, PyBUF_SIMPLE);
}

PyObject *pm_cpy_native_call(const char *fqn, const char *export_name, PyObject *args_tuple) {
    void *p = pm_wasmmod_registry_resolve_native((const uint8_t *)fqn, (uint32_t)strlen(fqn),
        (const uint8_t *)export_name, (uint32_t)strlen(export_name));
    if (p == NULL) {
        PyErr_SetString(PyExc_LookupError, "resolve miss");
        return NULL;
    }

    Py_ssize_t n_args = args_tuple == NULL ? 0 : PyTuple_GET_SIZE(args_tuple);
    char sig[160];
    (void)fetch_sig(fqn, export_name, sig, sizeof(sig));

    if (sig[0] == '\0' || strcmp(sig, "int32_t(void)") == 0) {
        if (n_args != 0 && sig[0] != '\0') {
            PyErr_SetString(PyExc_TypeError, "native call arity");
            return NULL;
        }
        if (n_args == 0) {
            return PyLong_FromLong(((int32_t (*)(void))p)());
        }
    }
    if (strcmp(sig, "int32_t(int32_t)") == 0 || (sig[0] == '\0' && n_args == 1)) {
        if (n_args != 1) {
            PyErr_SetString(PyExc_TypeError, "native call arity");
            return NULL;
        }
        long a0 = PyLong_AsLong(PyTuple_GET_ITEM(args_tuple, 0));
        if (a0 == -1 && PyErr_Occurred()) {
            return NULL;
        }
        return PyLong_FromLong(((int32_t (*)(int32_t))p)((int32_t)a0));
    }
    if (strcmp(sig, "int32_t(int32_t, int32_t)") == 0 || (sig[0] == '\0' && n_args == 2)) {
        if (n_args != 2) {
            PyErr_SetString(PyExc_TypeError, "native call arity");
            return NULL;
        }
        long a0 = PyLong_AsLong(PyTuple_GET_ITEM(args_tuple, 0));
        long a1 = PyLong_AsLong(PyTuple_GET_ITEM(args_tuple, 1));
        if ((a0 == -1 || a1 == -1) && PyErr_Occurred()) {
            return NULL;
        }
        return PyLong_FromLong(
            ((int32_t (*)(int32_t, int32_t))p)((int32_t)a0, (int32_t)a1));
    }
    if (strcmp(sig, "int32_t(int32_t, int32_t, int32_t)") == 0
        || (sig[0] == '\0' && n_args == 3)) {
        if (n_args != 3) {
            PyErr_SetString(PyExc_TypeError, "native call arity");
            return NULL;
        }
        long a0 = PyLong_AsLong(PyTuple_GET_ITEM(args_tuple, 0));
        long a1 = PyLong_AsLong(PyTuple_GET_ITEM(args_tuple, 1));
        long a2 = PyLong_AsLong(PyTuple_GET_ITEM(args_tuple, 2));
        if ((a0 == -1 || a1 == -1 || a2 == -1) && PyErr_Occurred()) {
            return NULL;
        }
        return PyLong_FromLong(((int32_t (*)(int32_t, int32_t, int32_t))p)(
            (int32_t)a0, (int32_t)a1, (int32_t)a2));
    }
    if (strcmp(sig, "int32_t(const uint8_t *, uint32_t)") == 0) {
        if (n_args != 1) {
            PyErr_SetString(PyExc_TypeError, "bufptr needs one bytes-like");
            return NULL;
        }
        Py_buffer view;
        if (get_buffer(PyTuple_GET_ITEM(args_tuple, 0), &view) != 0) {
            return NULL;
        }
        int32_t out = ((int32_t (*)(const uint8_t *, uint32_t))p)(
            (const uint8_t *)view.buf, (uint32_t)view.len);
        PyBuffer_Release(&view);
        return PyLong_FromLong(out);
    }
    if (strcmp(sig, "int32_t(pm_wasmmod_mem_cookie_t)") == 0) {
        if (n_args != 1) {
            PyErr_SetString(PyExc_TypeError, "mem needs one bytes-like");
            return NULL;
        }
        Py_buffer view;
        if (get_buffer(PyTuple_GET_ITEM(args_tuple, 0), &view) != 0) {
            return NULL;
        }
        pm_wasmmod_mem_cookie_t c =
            pm_wasmmod_mem_cookie_put((const uint8_t *)view.buf, (uint32_t)view.len);
        if (c == 0) {
            PyBuffer_Release(&view);
            PyErr_SetString(PyExc_RuntimeError, "mem cookie table full");
            return NULL;
        }
        int32_t out = ((int32_t (*)(pm_wasmmod_mem_cookie_t))p)(c);
        pm_wasmmod_mem_cookie_release(c);
        PyBuffer_Release(&view);
        return PyLong_FromLong(out);
    }
    if (strcmp(sig, "int32_t(pm_wasmmod_obj_handle_t)") == 0) {
        if (n_args != 1) {
            PyErr_SetString(PyExc_TypeError, "obj needs one argument");
            return NULL;
        }
        pm_wasmmod_obj_handle_t h = pm_wasmmod_obj_handle_put(PyTuple_GET_ITEM(args_tuple, 0));
        if (h == 0) {
            PyErr_SetString(PyExc_RuntimeError, "obj handle table full");
            return NULL;
        }
        int32_t out = ((int32_t (*)(pm_wasmmod_obj_handle_t))p)(h);
        pm_wasmmod_obj_handle_release(h);
        return PyLong_FromLong(out);
    }
    if (strcmp(sig, "int64_t(int64_t)") == 0) {
        if (n_args != 1) {
            PyErr_SetString(PyExc_TypeError, "native call arity");
            return NULL;
        }
        long long a0 = PyLong_AsLongLong(PyTuple_GET_ITEM(args_tuple, 0));
        if (a0 == -1 && PyErr_Occurred()) {
            return NULL;
        }
        return PyLong_FromLongLong(((int64_t (*)(int64_t))p)((int64_t)a0));
    }
    if (strcmp(sig, "float(float)") == 0) {
        if (n_args != 1) {
            PyErr_SetString(PyExc_TypeError, "native call arity");
            return NULL;
        }
        double a0 = PyFloat_AsDouble(PyTuple_GET_ITEM(args_tuple, 0));
        if (a0 == -1.0 && PyErr_Occurred()) {
            return NULL;
        }
        return PyFloat_FromDouble((double)((float (*)(float))p)((float)a0));
    }
    if (strcmp(sig, "double(double)") == 0) {
        if (n_args != 1) {
            PyErr_SetString(PyExc_TypeError, "native call arity");
            return NULL;
        }
        double a0 = PyFloat_AsDouble(PyTuple_GET_ITEM(args_tuple, 0));
        if (a0 == -1.0 && PyErr_Occurred()) {
            return NULL;
        }
        return PyFloat_FromDouble(((double (*)(double))p)(a0));
    }

    PyErr_SetString(PyExc_ValueError, "unsupported native sig (use CONNECT from C/RS)");
    return NULL;
}
