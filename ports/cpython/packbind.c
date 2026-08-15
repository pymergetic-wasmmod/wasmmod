/*
 * MPWP mount bind + export callables for the CPython host.
 * Score: reject .upy. / .mpy; prefer .cpy. then .py. Skip PYC (marshal version).
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "ports/cpython/packbind.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ports/cpython/finder.h"
#include "ports/cpython/hostready.h"
#include "pymergetic/wasmmod/api/__exports__.h"
#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/pack/__types__.h"
#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/format/elf/load.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (1)
#endif

#define ELF_CAPSULE "pm_cpy.wasm_elf"

static size_t pack_logical_path_len(const char *path, size_t path_len) {
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (path[i] != '.') {
            continue;
        }
        if (memcmp(path + i, ".upy.", 5) == 0 || memcmp(path + i, ".cpy.", 5) == 0) {
            return i;
        }
    }
    if (path_len >= 3 && memcmp(path + path_len - 3, ".py", 3) == 0) {
        return path_len - 3;
    }
    if (path_len >= 4 && memcmp(path + path_len - 4, ".mpy", 4) == 0) {
        return path_len - 4;
    }
    if (path_len >= 4 && memcmp(path + path_len - 4, ".pyc", 4) == 0) {
        return path_len - 4;
    }
    return path_len;
}

static bool pack_logical_eq(const char *a, size_t a_len, const char *b, size_t b_len) {
    size_t la = pack_logical_path_len(a, a_len);
    size_t lb = pack_logical_path_len(b, b_len);
    return la == lb && memcmp(a, b, la) == 0;
}

static int path_to_dotted(const char *root, size_t root_len, const char *path, size_t path_len,
    char *out, size_t cap) {
    size_t n = pack_logical_path_len(path, path_len);
    if (n == 8 && memcmp(path, "__init__", 8) == 0) {
        if (root_len + 1 > cap) {
            return -1;
        }
        memcpy(out, root, root_len);
        out[root_len] = '\0';
        return 0;
    }
    if (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0) {
        n -= 9;
    }
    if (path_len == 0) {
        if (root_len + 1 > cap) {
            return -1;
        }
        memcpy(out, root, root_len);
        out[root_len] = '\0';
        return 0;
    }
    size_t need = root_len + 1 + n + 1;
    if (need > cap) {
        return -1;
    }
    memcpy(out, root, root_len);
    out[root_len] = '.';
    for (size_t i = 0; i < n; ++i) {
        out[root_len + 1 + i] = path[i] == '/' ? '.' : path[i];
    }
    out[root_len + 1 + n] = '\0';
    return 0;
}

static bool path_is_package_init(const char *path, size_t path_len) {
    size_t n = pack_logical_path_len(path, path_len);
    return (n == 8 && memcmp(path, "__init__", 8) == 0)
        || (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0);
}

static int ensure_parent_packages(const char *full_name) {
    size_t len = strlen(full_name);
    for (size_t i = 0; i < len; ++i) {
        if (full_name[i] != '.') {
            continue;
        }
        char parent[256];
        if (i >= sizeof(parent)) {
            return -1;
        }
        memcpy(parent, full_name, i);
        parent[i] = '\0';
        if (pm_cpy_is_host_face(parent)) {
            continue;
        }
        PyObject *m = PyImport_AddModule(parent);
        if (m == NULL) {
            return -1;
        }
        if (!PyObject_HasAttrString(m, "__path__")) {
            PyObject *p = PyUnicode_FromStringAndSize(parent, (Py_ssize_t)i);
            if (p == NULL) {
                return -1;
            }
            if (PyObject_SetAttrString(m, "__path__", p) != 0) {
                Py_DECREF(p);
                return -1;
            }
            Py_DECREF(p);
        }
    }
    return 0;
}

static void link_module_to_parent(const char *dotted_name) {
    const char *dot = strrchr(dotted_name, '.');
    if (dot == NULL) {
        return;
    }
    char parent[256];
    size_t n = (size_t)(dot - dotted_name);
    if (n >= sizeof(parent)) {
        return;
    }
    memcpy(parent, dotted_name, n);
    parent[n] = '\0';
    PyObject *sysm = PyImport_GetModuleDict();
    PyObject *pmod = PyDict_GetItemString(sysm, parent);
    PyObject *cmod = PyDict_GetItemString(sysm, dotted_name);
    if (pmod == NULL || cmod == NULL) {
        return;
    }
    (void)PyObject_SetAttrString(pmod, dot + 1, cmod);
    PyErr_Clear();
}

static int score_pack_file_for_cpy_host(const mp_pack_manifest_file_t *f) {
    const char *path = f->path;
    size_t path_len = f->path_len;
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".upy.", 5) == 0) {
            return -1;
        }
    }
    if (f->kind == MP_PACK_KIND_MPY || f->kind == MP_PACK_KIND_PYC) {
        return -1;
    }
    if (f->kind != MP_PACK_KIND_PY) {
        return -1;
    }
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".cpy.", 5) == 0) {
            return 100;
        }
    }
    return 10;
}

static int exec_py_into_module(PyObject *module_obj, const char *src_name, const uint8_t *data,
    uint32_t len) {
    char *src = MICROPY_WASM_MALLOC((size_t)len + 1);
    if (src == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    memcpy(src, data, len);
    src[len] = '\0';
    PyObject *code = Py_CompileString(src, src_name, Py_file_input);
    MICROPY_WASM_FREE(src);
    if (code == NULL) {
        return -1;
    }
    PyObject *dict = PyModule_GetDict(module_obj);
    PyObject *res = PyEval_EvalCode(code, dict, dict);
    Py_DECREF(code);
    if (res == NULL) {
        return -1;
    }
    Py_DECREF(res);
    return 0;
}

static int exec_pack_file_into_module(PyObject *module_obj, const char *src_name,
    const mp_pack_manifest_file_t *f) {
    const uint8_t *data;
    uint32_t data_len;
    uint8_t *to_free = NULL;
    if (!mp_pack_manifest_file_bytes(f, &data, &data_len, &to_free)) {
        PyErr_SetString(PyExc_ValueError, "wasm pack file inflate failed");
        return -1;
    }
    int st = -1;
    if (f->kind == MP_PACK_KIND_PY) {
        st = exec_py_into_module(module_obj, src_name, data, data_len);
    } else {
        PyErr_Format(PyExc_ValueError, "wasm pack file kind %d not supported", (int)f->kind);
    }
    MICROPY_WASM_FREE(to_free);
    return st;
}

static PyObject *module_for_export_suffix(const char *pack_name, const char *suffix,
    uint16_t suffix_len) {
    if (suffix_len == 0 || (suffix_len == 1 && suffix[0] == '.')) {
        PyObject *m = PyImport_AddModule(pack_name);
        if (m != NULL) {
            Py_INCREF(m);
        }
        return m;
    }
    const char *dot = strrchr(pack_name, '.');
    const char *leaf = dot ? dot + 1 : pack_name;
    if (strlen(leaf) == suffix_len && memcmp(leaf, suffix, suffix_len) == 0) {
        PyObject *m = PyImport_AddModule(pack_name);
        if (m != NULL) {
            Py_INCREF(m);
        }
        return m;
    }
    char name[320];
    if ((size_t)snprintf(name, sizeof(name), "%s.%.*s", pack_name, (int)suffix_len, suffix)
        >= sizeof(name)) {
        PyErr_SetString(PyExc_ValueError, "export module name too long");
        return NULL;
    }
    if (ensure_parent_packages(name) != 0) {
        return NULL;
    }
    PyObject *m = PyImport_AddModule(name);
    if (m == NULL) {
        return NULL;
    }
    Py_INCREF(m);
    return m;
}

static int bind_pack_exports(const char *pack_name, const mp_pack_manifest_t *info) {
    if (info == NULL || info->n_exports == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < info->n_exports; ++i) {
        const mp_pack_manifest_export_t *ex = &info->exports[i];
        if (ex->func_len == 0 || ex->export_len == 0) {
            continue;
        }
        if (pm_wasmmod_registry_resolve_native((const uint8_t *)pack_name,
                (uint32_t)strlen(pack_name), (const uint8_t *)ex->export_name, ex->export_len)
            == NULL) {
            continue;
        }
        char export_name[128];
        size_t elen = ex->export_len < sizeof(export_name) - 1 ? ex->export_len
                                                              : sizeof(export_name) - 1;
        memcpy(export_name, ex->export_name, elen);
        export_name[elen] = '\0';
        char func[96];
        size_t flen = ex->func_len < sizeof(func) - 1 ? ex->func_len : sizeof(func) - 1;
        memcpy(func, ex->func, flen);
        func[flen] = '\0';
        PyObject *target = module_for_export_suffix(pack_name, ex->module, ex->module_len);
        if (target == NULL) {
            return -1;
        }
        PyObject *f = pm_cpy_make_export_fun(pack_name, export_name);
        if (f == NULL) {
            Py_DECREF(target);
            return -1;
        }
        int set = PyObject_SetAttrString(target, func, f);
        Py_DECREF(f);
        Py_DECREF(target);
        if (set != 0) {
            return -1;
        }
    }
    return 0;
}

PyObject *pm_cpy_pack_bind(const char *pack_name, pm_wasmmod_registry_handle_t h,
    const uint8_t *meta, uint32_t meta_len) {
    int32_t lc = 0;
    (void)pm_wasmmod_api_call0_i32((const uint8_t *)pack_name, (uint32_t)strlen(pack_name),
        (const uint8_t *)"mp_pack_load", 12, &lc);

    if (ensure_parent_packages(pack_name) != 0) {
        return NULL;
    }
    PyObject *root = PyImport_AddModule(pack_name);
    if (root == NULL) {
        return NULL;
    }
    Py_INCREF(root);
    pm_cpy_store_handle_on_module(root, h);

    PyObject *path = PyUnicode_FromString(pack_name);
    if (path != NULL) {
        (void)PyObject_SetAttrString(root, "__path__", path);
        Py_DECREF(path);
    }

    mp_pack_manifest_t info;
    memset(&info, 0, sizeof(info));
    bool have_pack = false;
    {
        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        have_pack = mp_pack_manifest_find_section(meta, meta_len, &payload, &payload_len)
            && mp_pack_manifest_parse(payload, payload_len, &info);
    }
    if (bind_pack_exports(pack_name, have_pack ? &info : NULL) != 0) {
        mp_pack_manifest_free(&info);
        Py_DECREF(root);
        return NULL;
    }

    if (have_pack) {
        uint32_t n_files = info.n_files ? info.n_files : 1;
        uint32_t *best_idx = MICROPY_WASM_MALLOC(n_files * sizeof(uint32_t));
        int *best_score = MICROPY_WASM_MALLOC(n_files * sizeof(int));
        if (best_idx == NULL || best_score == NULL) {
            MICROPY_WASM_FREE(best_idx);
            MICROPY_WASM_FREE(best_score);
            mp_pack_manifest_free(&info);
            Py_DECREF(root);
            PyErr_NoMemory();
            return NULL;
        }
        uint32_t n_best = 0;
        for (uint32_t i = 0; i < info.n_files; ++i) {
            const mp_pack_manifest_file_t *f = &info.files[i];
            if (f->kind != MP_PACK_KIND_PY && f->kind != MP_PACK_KIND_MPY
                && f->kind != MP_PACK_KIND_PYC) {
                continue;
            }
            int score = score_pack_file_for_cpy_host(f);
            if (score < 0) {
                continue;
            }
            uint32_t slot = n_best;
            bool found = false;
            for (uint32_t j = 0; j < n_best; ++j) {
                if (pack_logical_eq(f->path, f->path_len, info.files[best_idx[j]].path,
                        info.files[best_idx[j]].path_len)) {
                    slot = j;
                    found = true;
                    break;
                }
            }
            if (found) {
                if (score > best_score[slot]) {
                    best_idx[slot] = i;
                    best_score[slot] = score;
                }
            } else {
                best_idx[n_best] = i;
                best_score[n_best] = score;
                n_best++;
            }
        }
        for (uint32_t j = 0; j < n_best; ++j) {
            const mp_pack_manifest_file_t *f = &info.files[best_idx[j]];
            char dotted_name[320];
            if (path_to_dotted(pack_name, strlen(pack_name), f->path, f->path_len, dotted_name,
                    sizeof(dotted_name))
                != 0) {
                continue;
            }
            if (ensure_parent_packages(dotted_name) != 0) {
                MICROPY_WASM_FREE(best_idx);
                MICROPY_WASM_FREE(best_score);
                mp_pack_manifest_free(&info);
                Py_DECREF(root);
                return NULL;
            }
            PyObject *mod = PyImport_AddModule(dotted_name);
            if (mod == NULL) {
                MICROPY_WASM_FREE(best_idx);
                MICROPY_WASM_FREE(best_score);
                mp_pack_manifest_free(&info);
                Py_DECREF(root);
                return NULL;
            }
            pm_cpy_store_handle_on_module(mod, h);
            if (path_is_package_init(f->path, f->path_len) || strcmp(dotted_name, pack_name) == 0) {
                PyObject *p = PyUnicode_FromString(dotted_name);
                if (p != NULL) {
                    (void)PyObject_SetAttrString(mod, "__path__", p);
                    Py_DECREF(p);
                }
            }
            char src_name[384];
            snprintf(src_name, sizeof(src_name), "%s:%.*s", pack_name, (int)f->path_len, f->path);
            if (exec_pack_file_into_module(mod, src_name, f) != 0) {
                MICROPY_WASM_FREE(best_idx);
                MICROPY_WASM_FREE(best_score);
                mp_pack_manifest_free(&info);
                Py_DECREF(root);
                return NULL;
            }
            if (strcmp(dotted_name, pack_name) == 0) {
                if (bind_pack_exports(pack_name, &info) != 0) {
                    MICROPY_WASM_FREE(best_idx);
                    MICROPY_WASM_FREE(best_score);
                    mp_pack_manifest_free(&info);
                    Py_DECREF(root);
                    return NULL;
                }
            }
            link_module_to_parent(dotted_name);
        }
        MICROPY_WASM_FREE(best_idx);
        MICROPY_WASM_FREE(best_score);
    }
    mp_pack_manifest_free(&info);
    link_module_to_parent(pack_name);
    return root;
}

#ifndef MP_WASM_ELF_ADAPTER_SLOTS
#define MP_WASM_ELF_ADAPTER_SLOTS (32)
#endif

typedef struct {
    bool used;
    void *native;
    uint8_t sig;
    mp_wasm_elf_image_t *img;
} elf_adapter_slot_t;

static elf_adapter_slot_t elf_adapters[MP_WASM_ELF_ADAPTER_SLOTS];

static int32_t elf_adapter_invoke(unsigned idx, const pm_wasmmod_registry_value_t *args,
    uint32_t nargs, pm_wasmmod_registry_value_t *results, uint32_t nresults) {
    if (idx >= MP_WASM_ELF_ADAPTER_SLOTS || !elf_adapters[idx].used
        || elf_adapters[idx].native == NULL) {
        return -1;
    }
    uint8_t sig = elf_adapters[idx].sig;
    if (sig == MP_PACK_SIG_AUTO) {
        sig = (uint8_t)nargs;
    }
    if (nargs < sig || nresults < 1) {
        return -1;
    }
    int32_t r = 0;
    void *fn = elf_adapters[idx].native;
    switch (sig) {
        case 0:
            r = ((int32_t (*)(void))fn)();
            break;
        case 1:
            r = ((int32_t (*)(int32_t))fn)(args[0].of.i32);
            break;
        case 2:
            r = ((int32_t (*)(int32_t, int32_t))fn)(args[0].of.i32, args[1].of.i32);
            break;
        case 3:
            r = ((int32_t (*)(int32_t, int32_t, int32_t))fn)(args[0].of.i32, args[1].of.i32,
                args[2].of.i32);
            break;
        case 4:
            r = ((int32_t (*)(int32_t, int32_t, int32_t, int32_t))fn)(args[0].of.i32, args[1].of.i32,
                args[2].of.i32, args[3].of.i32);
            break;
        default:
            return -1;
    }
    results[0] = pm_wasmmod_registry_value_i32(r);
    return 0;
}

#define ELF_AD_FN(N)                                                                               \
    static int32_t elf_ad_##N(const pm_wasmmod_registry_value_t *a, uint32_t n,                    \
        pm_wasmmod_registry_value_t *r, uint32_t nr) {                                             \
        return elf_adapter_invoke(N, a, n, r, nr);                                                 \
    }
ELF_AD_FN(0) ELF_AD_FN(1) ELF_AD_FN(2) ELF_AD_FN(3)
ELF_AD_FN(4) ELF_AD_FN(5) ELF_AD_FN(6) ELF_AD_FN(7)
ELF_AD_FN(8) ELF_AD_FN(9) ELF_AD_FN(10) ELF_AD_FN(11)
ELF_AD_FN(12) ELF_AD_FN(13) ELF_AD_FN(14) ELF_AD_FN(15)
ELF_AD_FN(16) ELF_AD_FN(17) ELF_AD_FN(18) ELF_AD_FN(19)
ELF_AD_FN(20) ELF_AD_FN(21) ELF_AD_FN(22) ELF_AD_FN(23)
ELF_AD_FN(24) ELF_AD_FN(25) ELF_AD_FN(26) ELF_AD_FN(27)
ELF_AD_FN(28) ELF_AD_FN(29) ELF_AD_FN(30) ELF_AD_FN(31)

static pm_wasmmod_registry_fn_t const elf_ad_fns[MP_WASM_ELF_ADAPTER_SLOTS] = {
    elf_ad_0, elf_ad_1, elf_ad_2, elf_ad_3, elf_ad_4, elf_ad_5, elf_ad_6, elf_ad_7,
    elf_ad_8, elf_ad_9, elf_ad_10, elf_ad_11, elf_ad_12, elf_ad_13, elf_ad_14, elf_ad_15,
    elf_ad_16, elf_ad_17, elf_ad_18, elf_ad_19, elf_ad_20, elf_ad_21, elf_ad_22, elf_ad_23,
    elf_ad_24, elf_ad_25, elf_ad_26, elf_ad_27, elf_ad_28, elf_ad_29, elf_ad_30, elf_ad_31,
};

static int claim_elf_adapter(void *native, uint8_t sig, mp_wasm_elf_image_t *img) {
    for (int i = 0; i < MP_WASM_ELF_ADAPTER_SLOTS; ++i) {
        if (!elf_adapters[i].used) {
            elf_adapters[i].used = true;
            elf_adapters[i].native = native;
            elf_adapters[i].sig = sig;
            elf_adapters[i].img = img;
            return i;
        }
    }
    return -1;
}

static void *elf_resolve_import(const char *name, void *ctx) {
    (void)ctx;
    (void)name;
    return NULL;
}

typedef struct {
    pm_wasmmod_registry_handle_t h;
    mp_wasm_elf_image_t *img;
    const mp_pack_manifest_t *info;
} elf_pub_ctx_t;

static uint8_t sig_for_export_name(const mp_pack_manifest_t *info, const char *name) {
    if (info == NULL) {
        return MP_PACK_SIG_AUTO;
    }
    size_t nlen = strlen(name);
    for (uint32_t i = 0; i < info->n_exports; ++i) {
        const mp_pack_manifest_export_t *ex = &info->exports[i];
        if (ex->export_len == nlen && memcmp(ex->export_name, name, nlen) == 0) {
            return ex->sig;
        }
    }
    return MP_PACK_SIG_AUTO;
}

static void elf_export_cb(const char *name, void *addr, void *ctx_in) {
    elf_pub_ctx_t *ctx = ctx_in;
    if (name == NULL || addr == NULL || name[0] == '\0') {
        return;
    }
    uint8_t sig = sig_for_export_name(ctx->info, name);
    int slot = claim_elf_adapter(addr, sig, ctx->img);
    if (slot < 0) {
        return;
    }
    (void)pm_wasmmod_registry_export_set(ctx->h, (const uint8_t *)name, (uint32_t)strlen(name),
        PM_WASMMOD_REGISTRY_EXPORT_FN, (void *)elf_ad_fns[slot]);
}

pm_wasmmod_registry_handle_t pm_cpy_elf_publish(const char *pack_name, const uint8_t *bytes,
    uint32_t len, void **img_out, char *err, size_t err_len) {
    pm_wasmmod_registry_handle_t bad = { .index = UINT32_MAX, .generation = 0 };
    if (img_out) {
        *img_out = NULL;
    }
#if !MICROPY_PY_WASM_ELF
    (void)pack_name;
    (void)bytes;
    (void)len;
    if (err && err_len) {
        snprintf(err, err_len, "ELF disabled");
    }
    return bad;
#else
    mp_wasm_elf_image_t *img = NULL;
    if (!mp_wasm_elf_image_load(bytes, len, elf_resolve_import, NULL, &img, err, err_len)) {
        return bad;
    }
    pm_wasmmod_registry_handle_t h = pm_wasmmod_registry_publish((const uint8_t *)pack_name,
        (uint32_t)strlen(pack_name), PM_WASMMOD_REGISTRY_CONTAINER_ELF);
    if (h.index == UINT32_MAX) {
        mp_wasm_elf_image_free(img);
        if (err && err_len) {
            snprintf(err, err_len, "registry publish failed");
        }
        return bad;
    }
    (void)pm_wasmmod_loader_bake_pkg_version((const uint8_t *)pack_name,
        (uint32_t)strlen(pack_name), bytes, len);

    mp_pack_manifest_t info;
    memset(&info, 0, sizeof(info));
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    bool have = mp_pack_manifest_find_section(bytes, len, &payload, &payload_len)
        && mp_pack_manifest_parse(payload, payload_len, &info);

    elf_pub_ctx_t ctx = { .h = h, .img = img, .info = have ? &info : NULL };
    mp_wasm_elf_foreach_func(img, elf_export_cb, &ctx);
    mp_pack_manifest_free(&info);

    if (img_out) {
        *img_out = img;
    }
    return h;
#endif
}

void pm_cpy_store_elf_on_module(PyObject *mod, void *img) {
    if (mod == NULL) {
        return;
    }
    PyObject *cap = PyCapsule_New(img, ELF_CAPSULE, NULL);
    if (cap == NULL) {
        PyErr_Clear();
        return;
    }
    if (PyObject_SetAttrString(mod, "__wasm_elf__", cap) != 0) {
        PyErr_Clear();
    }
    Py_DECREF(cap);
}

void pm_cpy_elf_release_for_module(PyObject *mod) {
#if !MICROPY_PY_WASM_ELF
    (void)mod;
#else
    pm_wasmmod_registry_handle_t h;
    if (!pm_cpy_load_handle_from_module(mod, &h)) {
        return;
    }
    mp_wasm_elf_image_t *img = NULL;
    PyObject *cap = PyObject_GetAttrString(mod, "__wasm_elf__");
    if (cap != NULL) {
        if (PyCapsule_CheckExact(cap)) {
            img = (mp_wasm_elf_image_t *)PyCapsule_GetPointer(cap, ELF_CAPSULE);
        }
        Py_DECREF(cap);
        PyErr_Clear();
    }
    for (int i = 0; i < MP_WASM_ELF_ADAPTER_SLOTS; ++i) {
        if (elf_adapters[i].used && elf_adapters[i].img == img) {
            elf_adapters[i].used = false;
            elf_adapters[i].native = NULL;
            elf_adapters[i].img = NULL;
        }
    }
    if (img != NULL) {
        mp_wasm_elf_image_free(img);
    }
    (void)pm_wasmmod_registry_unpublish(h);
#endif
}
