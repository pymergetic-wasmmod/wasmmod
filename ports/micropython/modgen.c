/*
 * pymergetic.util.gen µPy face + optional wasmmod.gen alias.
 * Gated by MICROPY_PY_WASM_GEN.
 */

#include "ports/micropython/mpconfig_wasm.h"

#ifndef MICROPY_PY_WASM_GEN
#define MICROPY_PY_WASM_GEN (0)
#endif

#if MICROPY_PY_WASM_GEN

#include <string.h>
#include <stdio.h>

#include "extmod/vfs.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/stream.h"

#include "ports/micropython/importhook.h"
#include "ports/micropython/modgen.h"
#include "pymergetic/util/gen/__exports__.h"
#include "pymergetic/wasmmod/registry.h"

static vstr_t gen_pyi_buf;
static int gen_pyi_buf_inited;

/* Reserved words that cannot be a def name in valid Python. Mirrors
 * is_python_keyword in util/gen/__impl__.rs — keep both in sync. */
static int pyi_name_is_keyword(const char *s) {
    static const char *const kws[] = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
        "while", "with", "yield", NULL
    };
    const char *const *k;
    for (k = kws; *k != NULL; k++) {
        if (strcmp(*k, s) == 0) return 1;
    }
    return 0;
}

/* py attr of a C export name, same rule as gen's py_attr_from_export:
 * strip the fqn-derived prefix ("pm_" + dots-as-underscores + "_"), else the
 * last underscore segment. Returns 1 with attr filled. */
static int gen_py_attr_of(const char *fqn, const char *cname, char *attr, size_t attr_max) {
    char prefix[128];
    size_t n = 0;
    const char *p;
    const char *leaf;
    if (fqn == NULL || cname == NULL || attr == NULL || attr_max == 0) {
        return 0;
    }
    /* gen's rule: "pm_" + fqn-after-"pymergetic." with dots as underscores */
    p = fqn;
    if (strncmp(p, "pymergetic.", 11) == 0) {
        p += 11;
    }
    prefix[n++] = 'p';
    prefix[n++] = 'm';
    prefix[n++] = '_';
    while (*p != 0 && n + 1u < sizeof(prefix)) {
        prefix[n++] = *p == '.' ? '_' : *p;
        p++;
    }
    prefix[n] = 0;
    {
        char full[192];
        size_t flen;
        snprintf(full, sizeof(full), "%s_", prefix);
        flen = strlen(full);
        if (strncmp(cname, full, flen) == 0 && cname[flen] != 0) {
            snprintf(attr, attr_max, "%s", cname + flen);
            return 1;
        }
    }
    leaf = strrchr(cname, '_');
    leaf = leaf != NULL ? leaf + 1 : cname;
    if (*leaf == 0) {
        return 0;
    }
    snprintf(attr, attr_max, "%s", leaf);
    return 1;
}

/* The C export name whose py attr is `attr` (reverse of gen's
 * py_attr_from_export): enumerate the module's registry exports and keep the
 * one whose py attr matches. Returns 1 with cname filled, 0 when none. */
static int gen_cname_for(const uint8_t *fqn, uint32_t fqn_len, const char *attr,
    char *cname, size_t cname_max) {
    uint32_t n = pm_wasmmod_registry_export_count(fqn, fqn_len);
    char ename[128];
    char eattr[128];
    uint32_t i;
    if (attr == NULL || cname == NULL || cname_max == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        pm_wasmmod_registry_export_kind_t kind;
        uint32_t ename_len = (uint32_t)sizeof(ename) - 1u;
        uint32_t esig_len = 0;
        if (pm_wasmmod_registry_export_at(fqn, fqn_len, i,
                (uint8_t *)ename, &ename_len, &kind, NULL, &esig_len) != 0) {
            continue;
        }
        ename[ename_len] = 0;
        if (!gen_py_attr_of((const char *)fqn, ename, eattr, sizeof(eattr))) {
            continue;
        }
        if (strcmp(eattr, attr) == 0) {
            snprintf(cname, cname_max, "%s", ename);
            return 1;
        }
    }
    return 0;
}

/* Pull the prose doc for one export from the metal inspect card's extractor
 * (Phase 9). Returns NULL when the seat has no doc extractor or the export
 * has no doc block — the stub stays a bare def, never a wrong docstring. */
static const char *gen_doc_for(const uint8_t *fqn, uint32_t fqn_len, const char *cname) {
    const char *(*doc_fn)(const char *, const char *);
    char fqnbuf[192];
    /* Resolve through the registry, not a direct link: seats without the
     * metal inspect card (pure wasmmod builds) still compile this port and
     * get NULL here — the stub stays a bare def, never a wrong docstring. */
    doc_fn = (const char *(*)(const char *, const char *))
        pm_wasmmod_registry_resolve_native(
            (const uint8_t *)"pymergetic.metal.inspect",
            (uint32_t)strlen("pymergetic.metal.inspect"),
            (const uint8_t *)"pm_metal_inspect_doc",
            (uint32_t)strlen("pm_metal_inspect_doc"));
    if (doc_fn == NULL || fqn_len >= sizeof(fqnbuf)) {
        return NULL;
    }
    /* the provider's fqn bytes are length-delimited, not NUL-terminated */
    memcpy(fqnbuf, fqn, fqn_len);
    fqnbuf[fqn_len] = 0;
    return doc_fn(fqnbuf, cname);
}

/* First prose sentence of a JSON doc body (up to the first '.'), or NULL.
 * The full JSON belongs to /docs; a stub docstring wants plain prose. */
static const char *gen_doc_prose(const char *doc, char *buf, size_t buf_max) {
    const char *k;
    const char *v;
    size_t n = 0;
    if (doc == NULL || buf == NULL || buf_max < 2) {
        return NULL;
    }
    k = strstr(doc, "\"prose\":\"");
    if (k == NULL) {
        return NULL;
    }
    v = k + 9;
    while (v[n] != 0 && v[n] != '"' && n + 1u < buf_max) {
        if (v[n] == '\\' && v[n + 1] != 0 && n + 2u < buf_max) {
            buf[n] = v[n + 1];
            n += 2;
            continue;
        }
        if (v[n] == '\\' && v[n + 1] != 0) {
            break;   /* escape would overflow the buffer — stop here */
        }
        buf[n] = v[n];
        n++;
    }
    buf[n] = 0;
    if (n == 0) {
        return NULL;
    }
    return buf;
}

static int32_t gen_py_face_provider(void *ctx, const uint8_t *fqn, uint32_t fqn_len,
    uint8_t *buf, uint32_t *inout_len) {
    (void)ctx;
    if (fqn == NULL || fqn_len == 0 || inout_len == NULL) {
        return -1;
    }
    if (!gen_pyi_buf_inited) {
        vstr_init(&gen_pyi_buf, 256);
        gen_pyi_buf_inited = 1;
    }
    if (buf == NULL) {
        vstr_reset(&gen_pyi_buf);
        nlr_buf_t nlr;
        if (nlr_push(&nlr) != 0) {
            return 0;
        }
        qstr q = qstr_from_strn((const char *)fqn, fqn_len);
        mp_obj_t mod = mp_import_name(q, mp_const_empty_tuple, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_dict_t *globals = mp_obj_module_get_globals(mod);
        if (globals == NULL) {
            nlr_pop();
            return 0;
        }
        vstr_add_str(&gen_pyi_buf, "# DO NOT EDIT — generated by `pymergetic.util.gen` (live µPy import).\n# ");
        vstr_add_strn(&gen_pyi_buf, (const char *)fqn, fqn_len);
        vstr_add_str(&gen_pyi_buf, "\n\nfrom typing import Any\n\n");
        int any = 0;
        int any_kw = 0;
        mp_map_t *map = &globals->map;
        for (size_t i = 0; i < map->alloc; ++i) {
            if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_qstr(map->table[i].key)) {
                continue;
            }
            const char *name = qstr_str(MP_OBJ_QSTR_VALUE(map->table[i].key));
            if (name[0] == '_') {
                continue;
            }
            if (!mp_obj_is_callable(map->table[i].value)) {
                continue;
            }
            /* A keyword can never be a def name or an annotation target in
             * valid Python; the runtime attribute is real (loader setattr),
             * so it is exposed via module __getattr__ below instead. */
            if (pyi_name_is_keyword(name)) {
                any = 1;
                any_kw = 1;
                continue;
            }
            vstr_add_str(&gen_pyi_buf, "def ");
            vstr_add_str(&gen_pyi_buf, name);
            vstr_add_str(&gen_pyi_buf, "(*args: Any, **kwargs: Any) -> Any");
            /* Docstring from the card's authored comment block (Phase 9):
             * resolve the callable's C export through the registry, ask the
             * inspect extractor for its doc, and land the prose in the stub
             * so REPL help and type checkers read the same text. */
            {
                char cname[128];
                char prose[256];
                const char *doc = NULL;
                if (gen_cname_for(fqn, fqn_len, name, cname, sizeof(cname))) {
                    doc = gen_doc_prose(gen_doc_for(fqn, fqn_len, cname),
                        prose, sizeof(prose));
                }
                if (doc != NULL) {
                    vstr_add_str(&gen_pyi_buf, ":\n    \"\"\"");
                    vstr_add_str(&gen_pyi_buf, doc);
                    vstr_add_str(&gen_pyi_buf, "\"\"\"\n    ...\n\n");
                } else {
                    vstr_add_str(&gen_pyi_buf, ": ...\n\n");
                }
            }
            any = 1;
        }
        nlr_pop();
        if (!any) {
            return 0;
        }
        /* Keyword-named attributes (await, yield, ...) get a module-level
         * __getattr__ stub each — the only PEP 484 way to type an attribute
         * whose name is a reserved word. */
        if (any_kw) {
            vstr_add_str(&gen_pyi_buf, "from typing import Callable, Literal\n\n");
            for (size_t i = 0; i < map->alloc; ++i) {
                if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_qstr(map->table[i].key)) {
                    continue;
                }
                const char *name = qstr_str(MP_OBJ_QSTR_VALUE(map->table[i].key));
                if (name[0] == '_' || !pyi_name_is_keyword(name)) {
                    continue;
                }
                if (!mp_obj_is_callable(map->table[i].value)) {
                    continue;
                }
                vstr_add_str(&gen_pyi_buf,
                    "def __getattr__(name: Literal[\"");
                vstr_add_str(&gen_pyi_buf, name);
                vstr_add_str(&gen_pyi_buf, "\"]) -> Callable[..., Any]: ...\n\n");
            }
        }
        *inout_len = (uint32_t)gen_pyi_buf.len;
        return 1;
    }
    uint32_t n = *inout_len;
    if (n > gen_pyi_buf.len) {
        n = (uint32_t)gen_pyi_buf.len;
    }
    memcpy(buf, gen_pyi_buf.buf, n);
    *inout_len = n;
    return 1;
}

static int32_t gen_vfs_read(void *ctx, const uint8_t *path, uint32_t path_len,
    uint8_t *buf, uint32_t *inout_len) {
    (void)ctx;
    if (path == NULL || path_len == 0 || inout_len == NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return 0;
    }
    mp_obj_t path_obj = mp_obj_new_str((const char *)path, path_len);
    mp_obj_t mode = mp_obj_new_str("rb", 2);
    mp_obj_t open_args[2] = { path_obj, mode };
    mp_obj_t f = mp_vfs_open(2, open_args, (mp_map_t *)&mp_const_empty_map);
    const mp_stream_p_t *stream = mp_get_stream_raise(f, MP_STREAM_OP_READ);
    vstr_t data;
    vstr_init(&data, 256);
    uint8_t chunk[512];
    for (;;) {
        int err = 0;
        mp_uint_t n = stream->read(f, chunk, sizeof(chunk), &err);
        if (n == MP_STREAM_ERROR) {
            vstr_clear(&data);
            mp_stream_close(f);
            nlr_pop();
            return -1;
        }
        if (n == 0) {
            break;
        }
        vstr_add_strn(&data, (const char *)chunk, n);
    }
    mp_stream_close(f);
    nlr_pop();
    if (buf == NULL) {
        *inout_len = (uint32_t)data.len;
        vstr_clear(&data);
        return 1;
    }
    uint32_t n = *inout_len;
    if (n > data.len) {
        n = (uint32_t)data.len;
    }
    memcpy(buf, data.buf, n);
    *inout_len = n;
    vstr_clear(&data);
    return 1;
}

static int32_t gen_vfs_write(void *ctx, const uint8_t *path, uint32_t path_len,
    const uint8_t *data, uint32_t data_len) {
    (void)ctx;
    if (path == NULL || path_len == 0) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return -1;
    }
    mp_obj_t path_obj = mp_obj_new_str((const char *)path, path_len);
    mp_obj_t mode = mp_obj_new_str("wb", 2);
    mp_obj_t open_args[2] = { path_obj, mode };
    mp_obj_t f = mp_vfs_open(2, open_args, (mp_map_t *)&mp_const_empty_map);
    const mp_stream_p_t *stream = mp_get_stream_raise(f, MP_STREAM_OP_WRITE);
    int err = 0;
    mp_uint_t n = stream->write(f, data, data_len, &err);
    mp_stream_close(f);
    nlr_pop();
    if (n == MP_STREAM_ERROR || n != data_len) {
        return -1;
    }
    return 0;
}

static void gen_install_py_face(void) {
    pm_util_gen_set_py_face_provider(gen_py_face_provider, NULL);
}

static mp_obj_t mod_wasm_gen(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    gen_install_py_face();
    const char *root = ".";
    int check = 0;
    if (n_args >= 1 && args[0] != mp_const_none) {
        root = mp_obj_str_get_str(args[0]);
    }
    if (n_args >= 2) {
        check = mp_obj_is_true(args[1]);
    }
    int32_t rc = pm_util_gen_run((const uint8_t *)root, (uint32_t)strlen(root), check);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_gen_obj, 0, 2, mod_wasm_gen);

static mp_obj_t mod_util_gen_run(size_t n_args, const mp_obj_t *args) {
    return mod_wasm_gen(n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_util_gen_run_obj, 0, 2, mod_util_gen_run);

static mp_obj_t mod_util_gen_run_vfs(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    gen_install_py_face();
    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("run_vfs needs dir and fqn"));
    }
    const char *dir = mp_obj_str_get_str(args[0]);
    const char *fqn = mp_obj_str_get_str(args[1]);
    int check = 0;
    if (n_args >= 3) {
        check = mp_obj_is_true(args[2]);
    }
    pm_util_gen_vfs_ops_t ops = { .read = gen_vfs_read, .write = gen_vfs_write };
    int32_t rc = pm_util_gen_run_vfs(
        (const uint8_t *)dir, (uint32_t)strlen(dir),
        (const uint8_t *)fqn, (uint32_t)strlen(fqn),
        check, ops, NULL);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_util_gen_run_vfs_obj, 2, 3, mod_util_gen_run_vfs);

static mp_obj_t mod_util_gen_diff(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    gen_install_py_face();
    if (n_args < 1) {
        mp_raise_TypeError(MP_ERROR_TEXT("diff needs fqn"));
    }
    const char *fqn = mp_obj_str_get_str(args[0]);
    const uint8_t *h = NULL, *rs = NULL, *pyi = NULL;
    uint32_t h_len = 0, rs_len = 0, pyi_len = 0;
    size_t l;
    if (n_args >= 2 && args[1] != mp_const_none) {
        h = (const uint8_t *)mp_obj_str_get_data(args[1], &l);
        h_len = (uint32_t)l;
    }
    if (n_args >= 3 && args[2] != mp_const_none) {
        rs = (const uint8_t *)mp_obj_str_get_data(args[2], &l);
        rs_len = (uint32_t)l;
    }
    if (n_args >= 4 && args[3] != mp_const_none) {
        pyi = (const uint8_t *)mp_obj_str_get_data(args[3], &l);
        pyi_len = (uint32_t)l;
    }
    int32_t rc = pm_util_gen_diff_included(
        (const uint8_t *)fqn, (uint32_t)strlen(fqn),
        h, h_len, rs, rs_len, pyi, pyi_len);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_util_gen_diff_obj, 1, 4, mod_util_gen_diff);

static const mp_rom_map_elem_t mp_module_pymergetic_util_gen_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_util_dot_gen) },
    { MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&mod_util_gen_run_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_vfs), MP_ROM_PTR(&mod_util_gen_run_vfs_obj) },
    { MP_ROM_QSTR(MP_QSTR_diff), MP_ROM_PTR(&mod_util_gen_diff_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_util_gen_globals, mp_module_pymergetic_util_gen_globals_table);

const mp_obj_module_t mp_module_pymergetic_util_gen = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_util_gen_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_util_dot_gen, mp_module_pymergetic_util_gen);

#endif /* MICROPY_PY_WASM_GEN */
