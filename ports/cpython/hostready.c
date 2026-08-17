#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "ports/cpython/hostready.h"

#include <string.h>

#include "ports/cpython/nativecall.h"
#include "pymergetic/wasmmod/pyexport.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

typedef struct {
    PyObject_HEAD
    char fqn[96];
    char export_name[96];
} ReadyFunObject;

static PyObject *ready_fun_call(PyObject *self, PyObject *args, PyObject *kw) {
    (void)kw;
    ReadyFunObject *o = (ReadyFunObject *)self;
    return pm_cpy_native_call(o->fqn, o->export_name, args);
}

static PyTypeObject ReadyFunType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pymergetic.wasmmod.ready_fun",
    .tp_basicsize = sizeof(ReadyFunObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_call = ready_fun_call,
};

static int ready_fun_type_ready(void) {
    if (ReadyFunType.tp_dict != NULL) {
        return 0;
    }
    return PyType_Ready(&ReadyFunType);
}

static PyObject *make_ready_fun(const char *fqn, const char *export_name) {
    if (ready_fun_type_ready() != 0) {
        return NULL;
    }
    ReadyFunObject *o = PyObject_New(ReadyFunObject, &ReadyFunType);
    if (o == NULL) {
        return NULL;
    }
    strncpy(o->fqn, fqn, sizeof(o->fqn) - 1);
    o->fqn[sizeof(o->fqn) - 1] = '\0';
    strncpy(o->export_name, export_name, sizeof(o->export_name) - 1);
    o->export_name[sizeof(o->export_name) - 1] = '\0';
    return (PyObject *)o;
}

PyObject *pm_cpy_make_export_fun(const char *fqn, const char *export_name) {
    return make_ready_fun(fqn, export_name);
}

static void attr_from_export(const char *fqn, const char *cname, char *out, size_t out_sz) {
    char prefix[96];
    size_t pi = 0;
    prefix[pi++] = 'p';
    prefix[pi++] = 'm';
    prefix[pi++] = '_';
    const char *p = strchr(fqn, '.');
    if (p != NULL) {
        p++;
        while (*p != '\0' && pi + 1 < sizeof(prefix)) {
            prefix[pi++] = (*p == '.') ? '_' : *p;
            p++;
        }
    }
    if (pi + 1 < sizeof(prefix)) {
        prefix[pi++] = '_';
    }
    prefix[pi] = '\0';

    if (strncmp(cname, prefix, pi) == 0 && cname[pi] != '\0') {
        size_t n = strlen(cname + pi);
        if (n >= out_sz) {
            n = out_sz - 1;
        }
        memcpy(out, cname + pi, n);
        out[n] = '\0';
        return;
    }
    const char *us = strrchr(cname, '_');
    const char *src = (us != NULL && us[1] != '\0') ? us + 1 : cname;
    size_t n = strlen(src);
    if (n >= out_sz) {
        n = out_sz - 1;
    }
    memcpy(out, src, n);
    out[n] = '\0';
}

static void attach_registry_exports(const char *fqn, PyObject *module) {
    size_t flen = strlen(fqn);
    uint32_t n = pm_wasmmod_registry_export_count((const uint8_t *)fqn, (uint32_t)flen);
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t name[128];
        uint32_t name_len = sizeof(name);
        uint8_t sig[160];
        uint32_t sig_len = sizeof(sig);
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (!pm_wasmmod_registry_export_at((const uint8_t *)fqn, (uint32_t)flen, i, name, &name_len,
                &kind, sig, &sig_len)) {
            continue;
        }
        if (kind != PM_WASMMOD_REGISTRY_EXPORT_FN && kind != PM_WASMMOD_REGISTRY_EXPORT_BUFPTR
            && kind != PM_WASMMOD_REGISTRY_EXPORT_MEM && kind != PM_WASMMOD_REGISTRY_EXPORT_OBJ
            && kind != PM_WASMMOD_REGISTRY_EXPORT_I64 && kind != PM_WASMMOD_REGISTRY_EXPORT_F32
            && kind != PM_WASMMOD_REGISTRY_EXPORT_F64) {
            continue;
        }
        name[name_len < sizeof(name) ? name_len : sizeof(name) - 1] = '\0';
        char attr[96];
        attr_from_export(fqn, (const char *)name, attr, sizeof(attr));
        if (PyObject_HasAttrString(module, attr)) {
            continue;
        }
        PyObject *fun = make_ready_fun(fqn, (const char *)name);
        if (fun == NULL) {
            PyErr_Clear();
            continue;
        }
        if (PyObject_SetAttrString(module, attr, fun) != 0) {
            PyErr_Clear();
        }
        Py_DECREF(fun);
    }
}

int pm_cpy_host_ready(const char *fqn, PyObject *module) {
    if (fqn == NULL || module == NULL || !PyModule_Check(module)) {
        return -1;
    }
    int n = pm_wasmmod_pyexport_bind_module(fqn, module);
    attach_registry_exports(fqn, module);
    return n;
}
