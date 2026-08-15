#define PY_SSIZE_T_CLEAN
#include <Python.h>

/*
 * Thin CPython face for wasmmod — PyInit_wasmmod as pymergetic.wasmmod.
 *
 * Boot/load → ports/common/; import→ready → importhook/hostready/nativecall;
 * pyexport → ports/cpython/pyexport.c; objhandle twin for cookie/handle faces.
 */
#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif

#include <Python.h>

#include <string.h>

#include "ports/common/boot.h"
#include "ports/common/load.h"
#include "ports/cpython/finder.h"
#include "ports/cpython/hostready.h"
#include "ports/cpython/importhook.h"
#include "ports/cpython/modgen.h"
#include "ports/cpython/nativecall.h"
#include "ports/cpython/packbind.h"
#include "pymergetic/wasmmod/__version__.h"
#include "pymergetic/wasmmod/api/__exports__.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/net/cdn.h"
#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/format/common/format.h"
#include "pymergetic/wasmmod/registry/__exports__.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (1)
#endif

#ifndef PM_WASMMOD_HOST_SRC
#define PM_WASMMOD_HOST_SRC ""
#endif

#ifndef PM_WASMMOD_HOST_VERSION
#define PM_WASMMOD_HOST_VERSION PYMERGETIC_WASMMOD_VERSION
#endif

static int ensure_pkg_path(PyObject *pkg, const char *extra) {
    PyObject *path = PyObject_GetAttrString(pkg, "__path__");
    if (path == NULL) {
        PyErr_Clear();
        path = PyList_New(0);
        if (path == NULL) {
            return -1;
        }
        if (PyObject_SetAttrString(pkg, "__path__", path) != 0) {
            Py_DECREF(path);
            return -1;
        }
    }
    if (!PyList_Check(path)) {
        Py_DECREF(path);
        return -1;
    }
    PyObject *s = PyUnicode_FromString(extra);
    if (s == NULL) {
        Py_DECREF(path);
        return -1;
    }
    int found = 0;
    Py_ssize_t n = PyList_GET_SIZE(path);
    for (Py_ssize_t i = 0; i < n; i++) {
        if (PyObject_RichCompareBool(PyList_GET_ITEM(path, i), s, Py_EQ) == 1) {
            found = 1;
            break;
        }
    }
    if (!found) {
        if (PyList_Append(path, s) != 0) {
            Py_DECREF(s);
            Py_DECREF(path);
            return -1;
        }
    }
    Py_DECREF(s);
    Py_DECREF(path);
    return 0;
}

static int install_package_layout(PyObject *wasmmod) {
    PyObject *sys_modules = PyImport_GetModuleDict();
    PyObject *pkg = PyDict_GetItemString(sys_modules, "pymergetic");
    if (pkg == NULL) {
        pkg = PyModule_New("pymergetic");
        if (pkg == NULL) {
            return -1;
        }
        if (PyDict_SetItemString(sys_modules, "pymergetic", pkg) != 0) {
            Py_DECREF(pkg);
            return -1;
        }
        Py_DECREF(pkg); /* dict owns */
        pkg = PyDict_GetItemString(sys_modules, "pymergetic");
    }
    if (ensure_pkg_path(pkg, PM_WASMMOD_HOST_SRC "/pymergetic") != 0) {
        return -1;
    }
    if (PyObject_SetAttrString(pkg, "wasmmod", wasmmod) != 0) {
        return -1;
    }
    if (PyDict_SetItemString(sys_modules, "pymergetic.wasmmod", wasmmod) != 0) {
        return -1;
    }
    return 0;
}

static PyObject *mod_version(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    uint8_t buf[64];
    uint32_t n = sizeof(buf);
    if (pm_wasmmod_registry_version((const uint8_t *)"pymergetic.wasmmod",
            (uint32_t)strlen("pymergetic.wasmmod"), buf, &n)
        && n > 0) {
        return PyUnicode_FromStringAndSize((const char *)buf, (Py_ssize_t)n);
    }
    return PyUnicode_FromString(PM_WASMMOD_HOST_VERSION);
}

static PyObject *mod_has(PyObject *self, PyObject *args) {
    (void)self;
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    return PyBool_FromLong(
        pm_wasmmod_registry_has((const uint8_t *)name, (uint32_t)strlen(name)));
}

static PyObject *mod_call(PyObject *self, PyObject *args) {
    (void)self;
    const char *fqn;
    const char *exp;
    PyObject *argv = NULL;
    if (!PyArg_ParseTuple(args, "ss|O", &fqn, &exp, &argv)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    PyObject *tuple;
    if (argv == NULL) {
        tuple = PyTuple_New(0);
    } else if (PyTuple_Check(argv)) {
        tuple = argv;
        Py_INCREF(tuple);
    } else if (PyList_Check(argv)) {
        tuple = PyList_AsTuple(argv);
    } else {
        PyErr_SetString(PyExc_TypeError, "call argv must be list or tuple");
        return NULL;
    }
    if (tuple == NULL) {
        return NULL;
    }
    PyObject *res = pm_cpy_native_call(fqn, exp, tuple);
    Py_DECREF(tuple);
    return res;
}

static PyObject *mod_connect(PyObject *self, PyObject *args) {
    (void)self;
    const char *fqn;
    const char *exp;
    if (!PyArg_ParseTuple(args, "ss", &fqn, &exp)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    pm_wasmmod_registry_fn_t fn = NULL;
    int32_t st = pm_wasmmod_api_connect((const uint8_t *)fqn, (uint32_t)strlen(fqn),
        (const uint8_t *)exp, (uint32_t)strlen(exp), &fn);
    return PyBool_FromLong(st == 0 && fn != NULL);
}

static PyObject *mod_bind_py(PyObject *self, PyObject *args) {
    (void)self;
    const char *fqn;
    PyObject *mod = NULL;
    if (!PyArg_ParseTuple(args, "s|O", &fqn, &mod)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    if (mod == NULL) {
        mod = PyImport_ImportModule(fqn);
        if (mod == NULL) {
            return NULL;
        }
    } else {
        Py_INCREF(mod);
    }
    int n = pm_cpy_host_ready(fqn, mod);
    Py_DECREF(mod);
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "bind_py failed");
        return NULL;
    }
    return PyLong_FromLong(n);
}

static PyObject *mod_load(PyObject *self, PyObject *args) {
    (void)self;
    Py_buffer view;
    const char *fqn = "anon";
    if (!PyArg_ParseTuple(args, "y*|s", &view, &fqn)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        PyBuffer_Release(&view);
        return NULL;
    }
    pm_wasmmod_host_prepared_t prep;
    char err[160];
    if (pm_wasmmod_host_prepare((const uint8_t *)view.buf, (uint32_t)view.len, fqn, &prep, err,
            sizeof(err))
        != 0) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_ValueError, err);
        return NULL;
    }
    pm_wasmmod_registry_handle_t h;
    PyObject *m;
#if MICROPY_PY_WASM_ELF
    if (prep.kind == MP_WASM_KIND_ELF) {
        void *img = NULL;
        h = pm_cpy_elf_publish(fqn, prep.bytes, prep.len, &img, err, sizeof(err));
        if (h.index == UINT32_MAX) {
            MICROPY_WASM_FREE(prep.owned);
            PyBuffer_Release(&view);
            PyErr_Format(PyExc_OSError, "elf load: %s", err);
            return NULL;
        }
        m = pm_cpy_pack_bind(fqn, h, prep.bytes, prep.len);
        if (m != NULL) {
            pm_cpy_store_elf_on_module(m, img);
        }
        MICROPY_WASM_FREE(prep.owned);
        PyBuffer_Release(&view);
        return m;
    }
#endif
    h = pm_wasmmod_host_load_wasm(fqn, prep.bytes, prep.len);
    if (h.index == UINT32_MAX) {
        MICROPY_WASM_FREE(prep.owned);
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_OSError, "wasm load failed");
        return NULL;
    }
    m = pm_cpy_pack_bind(fqn, h, prep.bytes, prep.len);
    MICROPY_WASM_FREE(prep.owned);
    PyBuffer_Release(&view);
    return m;
}

static PyObject *mod_unload(PyObject *self, PyObject *args) {
    (void)self;
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    pm_cpy_unload_pack(name);
    Py_RETURN_NONE;
}

static PyObject *mod_path(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    PyObject *lst = pm_cpy_path_obj();
    if (lst == NULL) {
        return NULL;
    }
    Py_INCREF(lst);
    return lst;
}

static PyObject *mod_path_append(PyObject *self, PyObject *args) {
    (void)self;
    const char *root;
    if (!PyArg_ParseTuple(args, "s", &root)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    pm_cpy_path_append(root);
    Py_RETURN_NONE;
}

static void wasm_cdn_scrub_path_bases(void) {
    PyObject *lst = pm_cpy_path_obj();
    if (lst == NULL || !PyList_Check(lst)) {
        return;
    }
    Py_ssize_t n = PyList_GET_SIZE(lst);
    for (Py_ssize_t i = n; i > 0; --i) {
        PyObject *item = PyList_GET_ITEM(lst, i - 1);
        if (!PyUnicode_Check(item)) {
            continue;
        }
        const char *s = PyUnicode_AsUTF8(item);
        if (s != NULL && pm_wasmmod_net_cdn_url_is_base(s)) {
            if (PyList_SetSlice(lst, i - 1, i, NULL) != 0) {
                PyErr_Clear();
            }
        }
    }
}

static PyObject *mod_cdn(PyObject *self, PyObject *args) {
    (void)self;
    const char *url;
    const char *token = NULL;
    if (!PyArg_ParseTuple(args, "s|z", &url, &token)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    if (!pm_wasmmod_io_uri_is_http(url)) {
        PyErr_SetString(PyExc_ValueError, "cdn: url must be http(s)");
        return NULL;
    }
    pm_wasmmod_net_cdn_configure(url, token);
    wasm_cdn_scrub_path_bases();
    return PyUnicode_FromString(pm_wasmmod_net_cdn_driver_name());
}

static PyObject *mod_cdn_prepend(PyObject *self, PyObject *args) {
    (void)self;
    const char *url;
    const char *token = NULL;
    if (!PyArg_ParseTuple(args, "s|z", &url, &token)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    if (!pm_wasmmod_io_uri_is_http(url)) {
        PyErr_SetString(PyExc_ValueError, "cdn_prepend: url must be http(s)");
        return NULL;
    }
    if (pm_wasmmod_net_cdn_base_count() == 0) {
        pm_wasmmod_net_cdn_configure(url, token);
    } else {
        (void)pm_wasmmod_net_cdn_prepend(url, token);
    }
    wasm_cdn_scrub_path_bases();
    return PyUnicode_FromString(pm_wasmmod_net_cdn_driver_name());
}

static PyObject *mod_cdn_reset(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    pm_wasmmod_net_cdn_reset();
    Py_RETURN_NONE;
}

static PyObject *mod_catalog(PyObject *self, PyObject *args, PyObject *kw) {
    (void)self;
    const char *channel = "lead";
    static char *kwnames[] = { "channel", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|s", kwnames, &channel)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    char err[160];
    uint8_t *buf = NULL;
    uint32_t len = 0;
    if (pm_wasmmod_net_cdn_fetch_index(channel, &buf, &len, err, sizeof(err)) != 0) {
        PyErr_SetString(PyExc_OSError, err);
        return NULL;
    }
    PyObject *json = PyImport_ImportModule("json");
    if (json == NULL) {
        MICROPY_WASM_FREE(buf);
        return NULL;
    }
    PyObject *loads = PyObject_GetAttrString(json, "loads");
    Py_DECREF(json);
    if (loads == NULL) {
        MICROPY_WASM_FREE(buf);
        return NULL;
    }
    PyObject *text = PyUnicode_FromStringAndSize((const char *)buf, (Py_ssize_t)len);
    MICROPY_WASM_FREE(buf);
    if (text == NULL) {
        Py_DECREF(loads);
        return NULL;
    }
    PyObject *doc = PyObject_CallFunctionObjArgs(loads, text, NULL);
    Py_DECREF(loads);
    Py_DECREF(text);
    if (doc == NULL) {
        return NULL;
    }
    if (!PyDict_Check(doc)) {
        Py_DECREF(doc);
        PyErr_SetString(PyExc_ValueError, "catalog: index JSON root must be object");
        return NULL;
    }
    PyObject *packages = PyDict_GetItemString(doc, "packages");
    if (packages == NULL || !PyDict_Check(packages)) {
        Py_DECREF(doc);
        PyErr_SetString(PyExc_ValueError, "catalog: missing packages object");
        return NULL;
    }
    PyObject *out = PyList_New(0);
    if (out == NULL) {
        Py_DECREF(doc);
        return NULL;
    }
    Py_ssize_t pos = 0;
    PyObject *k, *v;
    while (PyDict_Next(packages, &pos, &k, &v)) {
        if (PyList_Append(out, k) != 0) {
            Py_DECREF(out);
            Py_DECREF(doc);
            return NULL;
        }
    }
    Py_DECREF(doc);
    return out;
}

static PyObject *mod_session_id(PyObject *self, PyObject *args) {
    (void)self;
    const char *sid_in = NULL;
    PyObject *arg = NULL;
    if (!PyArg_ParseTuple(args, "|O", &arg)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    if (arg != NULL) {
        if (arg == Py_None) {
            pm_wasmmod_net_cdn_set_session_id(NULL);
        } else if (PyUnicode_Check(arg)) {
            sid_in = PyUnicode_AsUTF8(arg);
            if (sid_in == NULL) {
                return NULL;
            }
            pm_wasmmod_net_cdn_set_session_id(sid_in);
        } else {
            PyErr_SetString(PyExc_TypeError, "session_id must be str or None");
            return NULL;
        }
    }
    const char *sid = pm_wasmmod_net_cdn_session_id();
    if (sid == NULL || sid[0] == '\0') {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromString(sid);
}

static PyObject *mod_publish(PyObject *self, PyObject *args, PyObject *kw) {
    (void)self;
    const char *name;
    const char *version;
    Py_buffer data;
    int lead = 1;
    int pin = 1;
    const char *token = NULL;
    static char *kwnames[] = { "name", "version", "data", "lead", "pin", "token", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kw, "ssy*|ppz", kwnames, &name, &version, &data, &lead,
            &pin, &token)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        PyBuffer_Release(&data);
        return NULL;
    }
    char err[160];
    int32_t st = pm_wasmmod_net_cdn_publish(name, version, data.buf, (uint32_t)data.len, lead, pin,
        token, err, sizeof(err));
    PyBuffer_Release(&data);
    if (st != 0) {
        if (strstr(err, "not supported") != NULL || strstr(err, "https requires") != NULL) {
            PyErr_SetString(PyExc_NotImplementedError, err);
        } else {
            PyErr_SetString(PyExc_OSError, err);
        }
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *mod_publish_file(PyObject *self, PyObject *args, PyObject *kw) {
    (void)self;
    const char *path;
    const char *name;
    const char *version;
    int lead = 1;
    int pin = 1;
    const char *token = NULL;
    static char *kwnames[] = { "path", "name", "version", "lead", "pin", "token", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kw, "sss|ppz", kwnames, &path, &name, &version, &lead,
            &pin, &token)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    uint8_t *bytes = NULL;
    size_t blen = 0;
    if (!pm_cpy_read_file(path, &bytes, &blen)) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, path);
        return NULL;
    }
    char err[160];
    int32_t st = pm_wasmmod_net_cdn_publish(name, version, bytes, (uint32_t)blen, lead, pin, token,
        err, sizeof(err));
    MICROPY_WASM_FREE(bytes);
    if (st != 0) {
        if (strstr(err, "not supported") != NULL || strstr(err, "https requires") != NULL) {
            PyErr_SetString(PyExc_NotImplementedError, err);
        } else {
            PyErr_SetString(PyExc_OSError, err);
        }
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *mod_install_hook(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    if (pm_cpy_install_hook() != 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *mod_uninstall_hook(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (pm_cpy_uninstall_hook() != 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *mod_publish_presence(PyObject *self, PyObject *args) {
    (void)self;
    const char *name;
    if (!PyArg_ParseTuple(args, "s", &name)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    pm_cpy_presence_publish(name);
    Py_RETURN_NONE;
}

static PyObject *mod_test(PyObject *self, PyObject *args) {
    (void)self;
    const char *fqn;
    const char *case_name = NULL;
    if (!PyArg_ParseTuple(args, "s|s", &fqn, &case_name)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    size_t flen = strlen(fqn);
    if (case_name != NULL) {
        return PyLong_FromLong(pm_wasmmod_registry_test_run((const uint8_t *)fqn, (uint32_t)flen,
            (const uint8_t *)case_name, (uint32_t)strlen(case_name)));
    }
    return PyLong_FromLong(pm_wasmmod_registry_test_run_all((const uint8_t *)fqn, (uint32_t)flen));
}

static PyObject *mod_test_all(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    uint32_t n = pm_wasmmod_registry_module_count();
    int32_t fails = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t buf[256];
        uint32_t len = sizeof(buf);
        if (!pm_wasmmod_registry_module_at(i, buf, &len) || len == 0) {
            continue;
        }
        if (pm_wasmmod_registry_test_count(buf, len) == 0) {
            continue;
        }
        fails += pm_wasmmod_registry_test_run_all(buf, len);
    }
    return PyLong_FromLong(fails);
}

static PyObject *mod_tests(PyObject *self, PyObject *args) {
    (void)self;
    const char *fqn;
    if (!PyArg_ParseTuple(args, "s", &fqn)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    size_t flen = strlen(fqn);
    uint32_t tc = pm_wasmmod_registry_test_count((const uint8_t *)fqn, (uint32_t)flen);
    PyObject *list = PyList_New(0);
    if (list == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < tc; i++) {
        uint8_t buf[128];
        uint32_t len = sizeof(buf);
        if (!pm_wasmmod_registry_test_at((const uint8_t *)fqn, (uint32_t)flen, i, buf, &len)
            || len == 0) {
            continue;
        }
        PyObject *s = PyUnicode_FromStringAndSize((const char *)buf, (Py_ssize_t)len);
        if (s == NULL || PyList_Append(list, s) != 0) {
            Py_XDECREF(s);
            Py_DECREF(list);
            return NULL;
        }
        Py_DECREF(s);
    }
    return list;
}

static PyObject *mod_test_count(PyObject *self, PyObject *args) {
    (void)self;
    const char *fqn;
    if (!PyArg_ParseTuple(args, "s", &fqn)) {
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        return NULL;
    }
    return PyLong_FromUnsignedLong(
        pm_wasmmod_registry_test_count((const uint8_t *)fqn, (uint32_t)strlen(fqn)));
}

static PyMethodDef wasmmod_methods[] = {
    {"version", mod_version, METH_NOARGS, NULL},
    {"has", mod_has, METH_VARARGS, NULL},
    {"call", mod_call, METH_VARARGS, NULL},
    {"connect", mod_connect, METH_VARARGS, NULL},
    {"bind_py", mod_bind_py, METH_VARARGS, NULL},
    {"load", mod_load, METH_VARARGS, NULL},
    {"unload", mod_unload, METH_VARARGS, NULL},
    {"path", mod_path, METH_NOARGS, NULL},
    {"path_append", mod_path_append, METH_VARARGS, NULL},
    {"cdn", mod_cdn, METH_VARARGS, NULL},
    {"cdn_prepend", mod_cdn_prepend, METH_VARARGS, NULL},
    {"cdn_reset", mod_cdn_reset, METH_NOARGS, NULL},
    {"catalog", (PyCFunction)(void (*)(void))mod_catalog, METH_VARARGS | METH_KEYWORDS, NULL},
    {"session_id", mod_session_id, METH_VARARGS, NULL},
    {"publish", (PyCFunction)(void (*)(void))mod_publish, METH_VARARGS | METH_KEYWORDS, NULL},
    {"publish_file", (PyCFunction)(void (*)(void))mod_publish_file, METH_VARARGS | METH_KEYWORDS,
        NULL},
    {"install_hook", mod_install_hook, METH_NOARGS, NULL},
    {"uninstall_hook", mod_uninstall_hook, METH_NOARGS, NULL},
    {"publish_presence", mod_publish_presence, METH_VARARGS, NULL},
    {"test", mod_test, METH_VARARGS, NULL},
    {"test_all", mod_test_all, METH_NOARGS, NULL},
    {"tests", mod_tests, METH_VARARGS, NULL},
    {"test_count", mod_test_count, METH_VARARGS, NULL},
    {"gen", pm_cpy_gen_run, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef wasmmod_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pymergetic.wasmmod",
    .m_doc = "wasmmod CPython host face",
    .m_size = -1,
    .m_methods = wasmmod_methods,
};

PyMODINIT_FUNC PyInit_wasmmod(void) {
    PyObject *m = PyModule_Create(&wasmmod_module);
    if (m == NULL) {
        return NULL;
    }
    if (install_package_layout(m) != 0) {
        Py_DECREF(m);
        return NULL;
    }
    if (pm_cpy_ensure_inited() != 0) {
        Py_DECREF(m);
        return NULL;
    }
    if (pm_cpy_install_gen() != 0) {
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
