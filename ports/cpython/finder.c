/*
 * Pack path finder — CPython twin of ports/micropython/finder.c.
 * Containers: .elf / .aotN / .aot / .wasm, each optionally + .zlib.
 * HTTP: io.probe / io.fetch. Artifact CDN bases skipped as flat HTTP roots.
 * Local: POSIX stat/fopen.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "ports/cpython/finder.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ports/common/load.h"
#include "ports/cpython/packbind.h"
#include "pymergetic/util/version/__exports__.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/net/cdn.h"
#include "pymergetic/wasmmod/pack/__types__.h"
#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/format/common/format.h"
#include "pymergetic/wasmmod/registry/__exports__.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (1)
#endif

#ifndef MICROPY_WASM_AOT_VERSION
#define MICROPY_WASM_AOT_VERSION (0)
#endif

#ifndef MICROPY_WASM_CONTAINERS
#define MICROPY_WASM_CONTAINERS "elf,aot,wasm"
#endif

#ifndef PM_CPY_PATH_CAP
#define PM_CPY_PATH_CAP (1536)
#endif

#ifndef PM_CPY_FQN_MAX
#define PM_CPY_FQN_MAX (192)
#endif

static PyObject *g_wasm_path;
static int g_import_depth;

bool pm_cpy_is_host_face(const char *dotted_name) {
    if (dotted_name == NULL || dotted_name[0] == '\0') {
        return false;
    }
    if (strcmp(dotted_name, "pymergetic") == 0) {
        return true;
    }
    /* Everything else the live registry answers: a name is a host face when a
     * RESIDENT card sits at it, above it, or below it. Guest packs come in a
     * WASM/AOT/ELF container and never match. No list of namespace roots here —
     * a downstream card tree is a host face because it registered, not because
     * this file was taught its name. Name relation first, container kind only
     * on a hit, so this stays one pass per import. */
    size_t nlen = strlen(dotted_name);
    uint32_t count = pm_wasmmod_registry_module_count();
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t fqn[PM_CPY_FQN_MAX];
        uint32_t flen = (uint32_t)sizeof(fqn);
        if (!pm_wasmmod_registry_module_at(i, fqn, &flen)) {
            continue;
        }
        bool related;
        if (flen == nlen) {
            related = memcmp(fqn, dotted_name, nlen) == 0;
        } else if (flen > nlen) {
            related = fqn[nlen] == '.' && memcmp(fqn, dotted_name, nlen) == 0;
        } else {
            related = dotted_name[flen] == '.' && memcmp(fqn, dotted_name, flen) == 0;
        }
        if (related
            && pm_wasmmod_registry_container(fqn, flen)
            == (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_RESIDENT) {
            return true;
        }
    }
    return false;
}

PyObject *pm_cpy_path_obj(void) {
    if (g_wasm_path == NULL) {
        g_wasm_path = PyList_New(0);
    }
    return g_wasm_path;
}

void pm_cpy_path_append(const char *root) {
    if (root == NULL || root[0] == '\0') {
        return;
    }
    PyObject *lst = pm_cpy_path_obj();
    if (lst == NULL) {
        return;
    }
    Py_ssize_t n = PyList_GET_SIZE(lst);
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PyList_GET_ITEM(lst, i);
        if (PyUnicode_Check(item)) {
            const char *s = PyUnicode_AsUTF8(item);
            if (s != NULL && strcmp(s, root) == 0) {
                return;
            }
        }
    }
    PyObject *s = PyUnicode_FromString(root);
    if (s == NULL) {
        PyErr_Clear();
        return;
    }
    if (PyList_Append(lst, s) != 0) {
        PyErr_Clear();
    }
    Py_DECREF(s);
}

static bool copy_path(char *out, size_t cap, const char *s) {
    size_t n = strlen(s);
    if (n + 1 > cap) {
        return false;
    }
    memcpy(out, s, n + 1);
    return true;
}

static void dotted_to_slash(const char *dotted, char *out, size_t cap) {
    size_t i = 0;
    for (; dotted[i] != '\0' && i + 1 < cap; ++i) {
        out[i] = dotted[i] == '.' ? '/' : dotted[i];
    }
    out[i] = '\0';
}

static bool is_regular_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool join_local(char *out, size_t cap, const char *root, const char *rel) {
    size_t rlen = root ? strlen(root) : 0;
    int n;
    if (rlen == 0) {
        n = snprintf(out, cap, "%s", rel);
    } else if (root[rlen - 1] == '/') {
        n = snprintf(out, cap, "%s%s", root, rel);
    } else {
        n = snprintf(out, cap, "%s/%s", root, rel);
    }
    return n > 0 && (size_t)n < cap;
}

static bool try_url_candidate(const char *root, const char *rel, char *path_out, size_t cap) {
    char uri[PM_CPY_PATH_CAP];
    pm_wasmmod_io_join_uri(root, rel, uri, (uint32_t)sizeof(uri));
    if (!pm_wasmmod_io_probe(uri)) {
        return false;
    }
    return copy_path(path_out, cap, uri);
}

static bool try_rel(const char *root, const char *rel, char *path_out, size_t cap) {
    if (pm_wasmmod_io_uri_is_http(root)) {
        return try_url_candidate(root, rel, path_out, cap);
    }
    char path[PM_CPY_PATH_CAP];
    if (!join_local(path, sizeof(path), root, rel)) {
        return false;
    }
    if (!is_regular_file(path)) {
        return false;
    }
    return copy_path(path_out, cap, path);
}

static bool try_rel_prefer_zlib(const char *root, const char *rel, char *path_out, size_t cap) {
    char zrel[PM_CPY_PATH_CAP];
    if (snprintf(zrel, sizeof(zrel), "%s.zlib", rel) < (int)sizeof(zrel)
        && try_rel(root, zrel, path_out, cap)) {
        return true;
    }
    return try_rel(root, rel, path_out, cap);
}

static bool try_stem_ext(const char *root, const char *stem, const char *ext, char *path_out,
    size_t cap) {
    char rel[PM_CPY_PATH_CAP];
    if (snprintf(rel, sizeof(rel), "%s%s", stem, ext) >= (int)sizeof(rel)) {
        return false;
    }
    return try_rel_prefer_zlib(root, rel, path_out, cap);
}

static bool try_pkg_init(const char *root, const char *stem, const char *ext, char *path_out,
    size_t cap) {
    char rel[PM_CPY_PATH_CAP];
    if (snprintf(rel, sizeof(rel), "%s/__init__%s", stem, ext) >= (int)sizeof(rel)) {
        return false;
    }
    return try_rel_prefer_zlib(root, rel, path_out, cap);
}

static bool try_one_ext(const char *root, const char *dotted, const char *slash, const char *ext,
    char *path_out, size_t cap) {
    if (try_stem_ext(root, dotted, ext, path_out, cap)) {
        return true;
    }
    if (try_stem_ext(root, slash, ext, path_out, cap)) {
        return true;
    }
    return try_pkg_init(root, slash, ext, path_out, cap);
}

static void aot_ext(char *buf, size_t buflen) {
    unsigned v = (unsigned)MICROPY_WASM_AOT_VERSION;
    if (v > 0) {
        snprintf(buf, buflen, ".aot%u", v);
    } else {
        snprintf(buf, buflen, ".aot");
    }
}

static bool try_aot(const char *root, const char *dotted, const char *slash, char *path_out,
    size_t cap) {
    char ext[16];
    aot_ext(ext, sizeof(ext));
    if (try_one_ext(root, dotted, slash, ext, path_out, cap)) {
        return true;
    }
    if (strcmp(ext, ".aot") != 0) {
        return try_one_ext(root, dotted, slash, ".aot", path_out, cap);
    }
    return false;
}

static bool try_candidates(const char *root, const char *dotted, const char *slash,
    char *path_out, size_t cap) {
    const char *pref = MICROPY_WASM_CONTAINERS;
    while (*pref) {
        while (*pref == ',' || *pref == ' ') {
            ++pref;
        }
        if (*pref == '\0') {
            break;
        }
        const char *start = pref;
        while (*pref && *pref != ',') {
            ++pref;
        }
        size_t n = (size_t)(pref - start);
        if (n == 3 && memcmp(start, "elf", 3) == 0) {
#if MICROPY_PY_WASM_ELF
            if (try_one_ext(root, dotted, slash, ".elf", path_out, cap)) {
                return true;
            }
#endif
        } else if (n == 3 && memcmp(start, "aot", 3) == 0) {
            if (try_aot(root, dotted, slash, path_out, cap)) {
                return true;
            }
        } else if (n == 4 && memcmp(start, "wasm", 4) == 0) {
            if (try_one_ext(root, dotted, slash, ".wasm", path_out, cap)) {
                return true;
            }
        }
    }
    return try_one_ext(root, dotted, slash, ".wasm", path_out, cap);
}

static bool find_in_list(PyObject *list_obj, const char *dotted, const char *slash, char *path_out,
    size_t cap) {
    if (list_obj == NULL || !PyList_Check(list_obj)) {
        return false;
    }
    Py_ssize_t n = PyList_GET_SIZE(list_obj);
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PyList_GET_ITEM(list_obj, i);
        if (!PyUnicode_Check(item)) {
            continue;
        }
        const char *root = PyUnicode_AsUTF8(item);
        if (root == NULL || strcmp(root, ".frozen") == 0) {
            continue;
        }
        if (pm_wasmmod_io_uri_is_http(root)
            && pm_wasmmod_net_cdn_driver() == PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS
            && pm_wasmmod_net_cdn_url_is_base(root)) {
            continue;
        }
        if (try_candidates(root, dotted, slash, path_out, cap)) {
            return true;
        }
    }
    return false;
}

bool pm_cpy_find_pack(const char *dotted_name, char *path_out, size_t path_cap) {
    if (dotted_name == NULL || dotted_name[0] == '\0' || pm_cpy_is_host_face(dotted_name)
        || path_out == NULL || path_cap == 0) {
        return false;
    }
    char slash[256];
    dotted_to_slash(dotted_name, slash, sizeof(slash));
    if (find_in_list(pm_cpy_path_obj(), dotted_name, slash, path_out, path_cap)) {
        return true;
    }
    PyObject *sys_path = PySys_GetObject("path");
    return find_in_list(sys_path, dotted_name, slash, path_out, path_cap);
}

bool pm_cpy_read_file(const char *path, uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    if (pm_wasmmod_io_uri_is_http(path)) {
        uint8_t *buf = NULL;
        uint32_t len = 0;
        char err[160];
        if (pm_wasmmod_io_fetch(path, &buf, &len, err, sizeof(err)) != 0 || buf == NULL || len == 0) {
            return false;
        }
        *out = buf;
        *out_len = len;
        return true;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    uint8_t *mem = MICROPY_WASM_MALLOC((size_t)sz);
    if (mem == NULL) {
        fclose(f);
        return false;
    }
    size_t n = fread(mem, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        MICROPY_WASM_FREE(mem);
        return false;
    }
    *out = mem;
    *out_len = n;
    return true;
}

void pm_cpy_store_handle_on_module(PyObject *mod, pm_wasmmod_registry_handle_t h) {
    PyObject *idx = PyLong_FromUnsignedLong(h.index);
    PyObject *gen = PyLong_FromUnsignedLong(h.generation);
    if (idx != NULL) {
        (void)PyObject_SetAttrString(mod, "__wasm_h_index__", idx);
        Py_DECREF(idx);
    }
    if (gen != NULL) {
        (void)PyObject_SetAttrString(mod, "__wasm_h_gen__", gen);
        Py_DECREF(gen);
    }
}

bool pm_cpy_load_handle_from_module(PyObject *mod, pm_wasmmod_registry_handle_t *out) {
    PyObject *idx = PyObject_GetAttrString(mod, "__wasm_h_index__");
    PyObject *gen = PyObject_GetAttrString(mod, "__wasm_h_gen__");
    if (idx == NULL || gen == NULL) {
        Py_XDECREF(idx);
        Py_XDECREF(gen);
        PyErr_Clear();
        return false;
    }
    out->index = (uint32_t)PyLong_AsUnsignedLong(idx);
    out->generation = (uint32_t)PyLong_AsUnsignedLong(gen);
    Py_DECREF(idx);
    Py_DECREF(gen);
    if (PyErr_Occurred()) {
        PyErr_Clear();
        return false;
    }
    return true;
}

static int load_local_deps(const uint8_t *bytes, uint32_t blen) {
    if (g_import_depth > 16) {
        PyErr_SetString(PyExc_RuntimeError, "wasm deps: nest limit");
        return -1;
    }
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (!mp_wasm_deps_find_section(bytes, blen, &payload, &payload_len)) {
        return 0;
    }
    mp_wasm_deps_info_t deps;
    memset(&deps, 0, sizeof(deps));
    if (!mp_wasm_deps_parse(payload, payload_len, &deps)) {
        return 0;
    }
    int st = 0;
    for (uint32_t i = 0; i < deps.n_deps; ++i) {
        if (deps.deps[i].name_len == 0) {
            continue;
        }
        char dep[256];
        size_t nlen = deps.deps[i].name_len;
        if (nlen >= sizeof(dep)) {
            nlen = sizeof(dep) - 1;
        }
        memcpy(dep, deps.deps[i].name, nlen);
        dep[nlen] = '\0';
        if (!pm_wasmmod_registry_has((const uint8_t *)dep, (uint32_t)strlen(dep))) {
            PyObject *sysm = PyImport_GetModuleDict();
            if (PyDict_GetItemString(sysm, dep) == NULL) {
                PyObject *got = pm_cpy_import_pack(dep);
                if (got == NULL) {
                    st = -1;
                    break;
                }
                Py_DECREF(got);
            }
        }
        if (deps.deps[i].version_len > 0
            && !(deps.deps[i].version_len == 1 && deps.deps[i].version[0] == '*')) {
            uint8_t have_buf[64];
            uint32_t have_len = sizeof(have_buf);
            if (!pm_wasmmod_registry_version((const uint8_t *)dep, (uint32_t)strlen(dep), have_buf,
                    &have_len)
                || have_len == 0) {
                PyErr_Format(PyExc_ImportError, "wasm dep '%s' has no registered version", dep);
                st = -1;
                break;
            }
            int32_t ok = pm_util_version_satisfies(have_buf, have_len,
                (const uint8_t *)deps.deps[i].version, deps.deps[i].version_len);
            if (ok == 0) {
                PyErr_Format(PyExc_ImportError, "wasm dep '%s' version mismatch", dep);
                st = -1;
                break;
            }
            if (ok < 0) {
                PyErr_Format(PyExc_ImportError, "wasm dep '%s' version unparsable", dep);
                st = -1;
                break;
            }
        }
    }
    mp_wasm_deps_info_free(&deps);
    return st;
}

PyObject *pm_cpy_import_pack(const char *dotted_name) {
    PyObject *sysm = PyImport_GetModuleDict();
    PyObject *existing = PyDict_GetItemString(sysm, dotted_name);
    if (existing != NULL) {
        Py_INCREF(existing);
        return existing;
    }
    if (pm_cpy_is_host_face(dotted_name)) {
        PyErr_Format(PyExc_ImportError, "host face '%s' is not a guest pack", dotted_name);
        return NULL;
    }

    uint8_t *raw = NULL;
    size_t raw_len = 0;
    char path[PM_CPY_PATH_CAP];
    char err[160];
    pm_wasmmod_host_prepared_t prep;
    if (pm_cpy_find_pack(dotted_name, path, sizeof(path))) {
        if (!pm_cpy_read_file(path, &raw, &raw_len)) {
            PyErr_SetFromErrnoWithFilename(PyExc_OSError, path);
            return NULL;
        }
    } else if (pm_wasmmod_net_cdn_driver() == PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS) {
        uint8_t *buf = NULL;
        uint32_t n = 0;
        if (pm_wasmmod_net_cdn_fetch_pack(dotted_name, NULL, &buf, &n, err, sizeof(err)) != 0) {
            PyErr_Format(PyExc_ImportError, "no pack for '%s' (%s)", dotted_name, err);
            return NULL;
        }
        raw = buf;
        raw_len = n;
    } else {
        PyErr_Format(PyExc_ImportError, "no pack for '%s'", dotted_name);
        return NULL;
    }

    memset(&prep, 0, sizeof(prep));
    if (pm_wasmmod_host_prepare(raw, (uint32_t)raw_len, dotted_name, &prep, err, sizeof(err)) != 0) {
        MICROPY_WASM_FREE(prep.owned);
        MICROPY_WASM_FREE(raw);
        PyErr_SetString(PyExc_ValueError, err);
        return NULL;
    }

    g_import_depth++;
    int dep_st = load_local_deps(prep.bytes, prep.len);
    g_import_depth--;
    if (dep_st != 0) {
        MICROPY_WASM_FREE(prep.owned);
        MICROPY_WASM_FREE(raw);
        return NULL;
    }

    pm_wasmmod_registry_handle_t h = { .index = UINT32_MAX, .generation = 0 };
    PyObject *mod = NULL;

#if MICROPY_PY_WASM_ELF
    if (prep.kind == MP_WASM_KIND_ELF) {
        void *img = NULL;
        h = pm_cpy_elf_publish(dotted_name, prep.bytes, prep.len, &img, err, sizeof(err));
        if (h.index == UINT32_MAX) {
            MICROPY_WASM_FREE(prep.owned);
            MICROPY_WASM_FREE(raw);
            PyErr_Format(PyExc_OSError, "elf load: %s", err);
            return NULL;
        }
        mod = pm_cpy_pack_bind(dotted_name, h, prep.bytes, prep.len);
        if (mod != NULL) {
            pm_cpy_store_elf_on_module(mod, img);
        }
        MICROPY_WASM_FREE(prep.owned);
        MICROPY_WASM_FREE(raw);
        return mod;
    }
#else
    if (prep.kind == MP_WASM_KIND_ELF) {
        MICROPY_WASM_FREE(prep.owned);
        MICROPY_WASM_FREE(raw);
        PyErr_SetString(PyExc_RuntimeError, "ELF disabled (MICROPY_PY_WASM_ELF=0)");
        return NULL;
    }
#endif

    h = pm_wasmmod_host_load_wasm(dotted_name, prep.bytes, prep.len);
    if (h.index == UINT32_MAX) {
        MICROPY_WASM_FREE(prep.owned);
        MICROPY_WASM_FREE(raw);
        PyErr_SetString(PyExc_OSError, "wasm load failed");
        return NULL;
    }
    mod = pm_cpy_pack_bind(dotted_name, h, prep.bytes, prep.len);
    MICROPY_WASM_FREE(prep.owned);
    MICROPY_WASM_FREE(raw);
    return mod;
}

void pm_cpy_unload_pack(const char *dotted_name) {
    if (dotted_name == NULL || dotted_name[0] == '\0') {
        return;
    }
    size_t nlen = strlen(dotted_name);
    PyObject *sysm = PyImport_GetModuleDict();
    PyObject *mod = PyDict_GetItemString(sysm, dotted_name);
    if (mod != NULL && PyModule_Check(mod)) {
        if (PyObject_HasAttrString(mod, "__wasm_elf__")) {
            pm_cpy_elf_release_for_module(mod);
        } else {
            pm_wasmmod_registry_handle_t h;
            if (pm_cpy_load_handle_from_module(mod, &h)) {
                (void)pm_wasmmod_loader_unload(h);
            }
        }
    }

    PyObject *keys = PyList_New(0);
    if (keys == NULL) {
        PyErr_Clear();
        return;
    }
    Py_ssize_t pos = 0;
    PyObject *key, *value;
    while (PyDict_Next(sysm, &pos, &key, &value)) {
        if (!PyUnicode_Check(key)) {
            continue;
        }
        const char *k = PyUnicode_AsUTF8(key);
        if (k == NULL) {
            continue;
        }
        size_t klen = strlen(k);
        if ((klen == nlen && memcmp(k, dotted_name, nlen) == 0)
            || (klen > nlen && k[nlen] == '.' && memcmp(k, dotted_name, nlen) == 0)) {
            if (PyList_Append(keys, key) != 0) {
                PyErr_Clear();
            }
        }
    }
    Py_ssize_t nk = PyList_GET_SIZE(keys);
    for (Py_ssize_t i = 0; i < nk; ++i) {
        (void)PyDict_DelItem(sysm, PyList_GET_ITEM(keys, i));
        PyErr_Clear();
    }
    Py_DECREF(keys);
}
