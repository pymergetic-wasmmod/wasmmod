#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "ports/cpython/importhook.h"

#include <string.h>

#include "ports/common/boot.h"
#include "ports/cpython/finder.h"
#include "ports/cpython/hostready.h"
#include "pymergetic/wasmmod/__version__.h"
#include "pymergetic/wasmmod/net/cdn.h"

#ifndef PM_WASMMOD_HOST_VERSION
#define PM_WASMMOD_HOST_VERSION PYMERGETIC_WASMMOD_VERSION
#endif

static int g_inited;
static int g_hook_depth;
static PyObject *g_prev_import;
static PyObject *g_hook_fn;

static const char *hook_name(PyObject *args) {
    if (args == NULL || !PyTuple_Check(args) || PyTuple_GET_SIZE(args) < 1) {
        return NULL;
    }
    PyObject *name_obj = PyTuple_GET_ITEM(args, 0);
    if (!PyUnicode_Check(name_obj)) {
        return NULL;
    }
    return PyUnicode_AsUTF8(name_obj);
}

static int already_loaded(const char *name) {
    PyObject *sys_modules = PyImport_GetModuleDict();
    return PyDict_GetItemString(sys_modules, name) != NULL;
}

void pm_cpy_presence_publish(const char *name) {
    pm_wasmmod_host_presence_publish(name);
}

static void after_import(const char *name) {
    pm_cpy_presence_publish(name);
    if (name == NULL || strncmp(name, "pymergetic.", 11) != 0) {
        return;
    }
    if (strcmp(name, "pymergetic.util") == 0 || strcmp(name, "pymergetic.wasmmod") == 0
        || strcmp(name, "pymergetic") == 0) {
        return;
    }
    PyObject *sys_modules = PyImport_GetModuleDict();
    PyObject *mod = PyDict_GetItemString(sys_modules, name);
    if (mod != NULL && PyModule_Check(mod)) {
        (void)pm_cpy_host_ready(name, mod);
    }
}

static PyObject *import_hook(PyObject *self, PyObject *args, PyObject *kw) {
    (void)self;
    if (g_prev_import == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "import hook not installed");
        return NULL;
    }
    const char *name = hook_name(args);

    if (g_hook_depth == 0 && name != NULL && !already_loaded(name)) {
        char path[1536];
        if (pm_cpy_find_pack(name, path, sizeof(path))) {
            g_hook_depth++;
            PyObject *pack = pm_cpy_import_pack(name);
            g_hook_depth--;
            if (pack == NULL) {
                return NULL;
            }
            Py_DECREF(pack);
            PyObject *res = PyObject_Call(g_prev_import, args, kw);
            if (res != NULL) {
                after_import(name);
            }
            return res;
        }
    }

    PyObject *res = PyObject_Call(g_prev_import, args, kw);
    if (res != NULL) {
        if (name != NULL) {
            after_import(name);
        }
        return res;
    }
    if (!PyErr_ExceptionMatches(PyExc_ImportError) || g_hook_depth > 0 || name == NULL) {
        return NULL;
    }

    PyObject *type, *value, *tb;
    PyErr_Fetch(&type, &value, &tb);
    g_hook_depth++;
    PyObject *pack = pm_cpy_import_pack(name);
    g_hook_depth--;
    if (pack == NULL) {
        if (PyErr_ExceptionMatches(PyExc_ImportError)) {
            PyErr_Clear();
            PyErr_Restore(type, value, tb);
        } else {
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(tb);
        }
        return NULL;
    }
    Py_DECREF(pack);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(tb);
    res = PyObject_Call(g_prev_import, args, kw);
    if (res != NULL) {
        after_import(name);
    }
    return res;
}

static PyMethodDef import_hook_def = {
    "__import__", (PyCFunction)(void (*)(void))import_hook, METH_VARARGS | METH_KEYWORDS, NULL
};

static void sync_sys_modules(void) {
    PyObject *sys_modules = PyImport_GetModuleDict();
    Py_ssize_t pos = 0;
    PyObject *key, *value;
    while (PyDict_Next(sys_modules, &pos, &key, &value)) {
        if (!PyUnicode_Check(key)) {
            continue;
        }
        const char *name = PyUnicode_AsUTF8(key);
        if (name != NULL) {
            pm_cpy_presence_publish(name);
        }
    }
}

int pm_cpy_install_hook(void) {
    if (g_prev_import != NULL) {
        sync_sys_modules();
        return 0;
    }
    PyObject *builtins = PyEval_GetBuiltins();
    if (builtins == NULL) {
        return -1;
    }
    PyObject *prev = PyDict_GetItemString(builtins, "__import__");
    if (prev == NULL) {
        return -1;
    }
    Py_INCREF(prev);
    g_prev_import = prev;
    g_hook_fn = PyCFunction_New(&import_hook_def, NULL);
    if (g_hook_fn == NULL) {
        Py_CLEAR(g_prev_import);
        return -1;
    }
    if (PyDict_SetItemString(builtins, "__import__", g_hook_fn) != 0) {
        Py_CLEAR(g_hook_fn);
        Py_CLEAR(g_prev_import);
        return -1;
    }
    sync_sys_modules();
    return 0;
}

int pm_cpy_uninstall_hook(void) {
    if (g_prev_import == NULL) {
        return 0;
    }
    PyObject *builtins = PyEval_GetBuiltins();
    if (builtins != NULL) {
        (void)PyDict_SetItemString(builtins, "__import__", g_prev_import);
    }
    Py_CLEAR(g_hook_fn);
    Py_CLEAR(g_prev_import);
    pm_wasmmod_net_cdn_reset();
    return 0;
}

int pm_cpy_ensure_inited(void) {
    if (!g_inited) {
        if (pm_wasmmod_host_boot("pymergetic.wasmmod", PM_WASMMOD_HOST_VERSION) != 0) {
            PyErr_SetString(PyExc_RuntimeError, "wasmmod loader_init failed");
            return -1;
        }
        g_inited = 1;
        (void)pm_cpy_path_obj();
    }
    if (g_prev_import == NULL) {
        if (pm_cpy_install_hook() != 0) {
            return -1;
        }
    }
    return 0;
}
