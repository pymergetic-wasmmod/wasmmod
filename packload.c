/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/emitglue.h"
#include "py/mperrno.h"
#include "py/objmodule.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#include "py/smallint.h"

#if MICROPY_PY_WASM

#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/finder.h"
#include "extmod/wasmmod/forward.h"
#include "extmod/wasmmod/cdn.h"
#include "extmod/wasmmod/resolve.h"
#include "extmod/wasmmod/mod.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/zlibutil.h"
#include "extmod/wasmmod/alloc.h"

int mp_wasm_import_hook_depth;

static bool ends_with(const char *s, const char *suf) {
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && strcmp(s + n - m, suf) == 0;
}

// Replace vstr contents with unwrapped MPZL payload when present.
static bool vstr_unwrap_artifact_zlib(vstr_t *buf) {
    const uint8_t *p = (const uint8_t *)buf->buf;
    uint32_t len = (uint32_t)buf->len;
    uint8_t *owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&p, &len, &owned)) {
        return false;
    }
    if (owned != NULL) {
        vstr_reset(buf);
        vstr_add_strn(buf, (const char *)owned, len);
        MICROPY_WASM_FREE(owned);
    }
    return true;
}

// Fetch path, preferring path+".zlib" when path is not already wrapped.
static bool fetch_artifact(const char *path, vstr_t *out, char *err, size_t err_len) {
    if (!ends_with(path, ".zlib")) {
        vstr_t zpath;
        size_t n = strlen(path);
        vstr_init(&zpath, n + 6);
        vstr_add_strn(&zpath, path, n);
        vstr_add_str(&zpath, ".zlib");
        if (mp_wasm_fetch(vstr_null_terminated_str(&zpath), out, err, err_len)) {
            vstr_clear(&zpath);
            if (vstr_unwrap_artifact_zlib(out)) {
                return true;
            }
            vstr_clear(out);
        } else {
            vstr_clear(&zpath);
        }
    }
    if (!mp_wasm_fetch(path, out, err, err_len)) {
        return false;
    }
    return vstr_unwrap_artifact_zlib(out);
}

// Path used for .wasm/.aot branching (strip trailing .zlib).
static const char *logical_artifact_path(const char *path, vstr_t *storage) {
    if (ends_with(path, ".zlib")) {
        size_t n = strlen(path) - 5;
        vstr_init(storage, n + 1);
        vstr_add_strn(storage, path, n);
        return vstr_null_terminated_str(storage);
    }
    return path;
}

#if MICROPY_PY_WASM_AOT
// Strip trailing .aot / .aotN and append new_suf (e.g. ".wasm").
static bool replace_aot_suffix(const char *path, const char *new_suf, vstr_t *out) {
    size_t alen = mp_wasm_aot_suffix_len(path);
    if (alen == 0) {
        return false;
    }
    size_t n = strlen(path);
    vstr_init(out, n - alen + strlen(new_suf) + 1);
    vstr_add_strn(out, path, n - alen);
    vstr_add_str(out, new_suf);
    return true;
}

// Replace suffix in path (e.g. .wasm → .aot6). Writes into out.
static bool replace_suffix(const char *path, const char *old_suf, const char *new_suf, vstr_t *out) {
    size_t n = strlen(path), m = strlen(old_suf);
    if (n < m || strcmp(path + n - m, old_suf) != 0) {
        return false;
    }
    vstr_init(out, n - m + strlen(new_suf) + 1);
    vstr_add_strn(out, path, n - m);
    vstr_add_str(out, new_suf);
    return true;
}
#endif
typedef struct {
    mp_obj_t py_mod;
    mp_pack_t *wasm;
} bind_ctx_t;

static void bind_export_cb(const char *name, uint32_t nparams, uint32_t nresults, void *ctx_in) {
    (void)nparams;
    (void)nresults;
    bind_ctx_t *ctx = ctx_in;
    if (strcmp(name, "mp_pack_load") == 0 || strcmp(name, "mp_pack_unload") == 0) {
        return;
    }
    mp_obj_t f = mp_wasm_func_new(ctx->wasm, qstr_from_str(name));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(ctx->py_mod)),
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), f);
}

// Logical module path length: strip host tags / extensions.
//   util.py                         → util
//   util.upy.mpy6.sib31.mpy         → util
//   util.cpy.cp312.pyc              → util
//   sub/mod.upy.mpy6.sib63.mpy      → sub/mod
static size_t pack_logical_path_len(const char *path, size_t path_len) {
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (path[i] != '.') {
            continue;
        }
        if (i + 5 <= path_len && memcmp(path + i, ".upy.", 5) == 0) {
            return i;
        }
        if (i + 5 <= path_len && memcmp(path + i, ".cpy.", 5) == 0) {
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

static void path_to_dotted(const char *root, size_t root_len, const char *path, size_t path_len, vstr_t *out) {
    vstr_init(out, root_len + path_len + 4);
    vstr_add_strn(out, root, root_len);
    if (path_len == 0) {
        return;
    }
    size_t n = pack_logical_path_len(path, path_len);
    if (n == 8 && memcmp(path, "__init__", 8) == 0) {
        return;
    }
    if (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0) {
        n -= 9;
    }
    vstr_add_char(out, '.');
    for (size_t i = 0; i < n; ++i) {
        char c = path[i];
        vstr_add_char(out, c == '/' ? '.' : c);
    }
}

// Score a pack file for this MicroPython host. <0 → skip.
// Prefer compatible .mpy (higher sib that still fits), else .py. Ignore .pyc.
static int score_pack_file_for_upy_host(const mp_pack_manifest_file_t *f) {
    const char *path = f->path;
    size_t path_len = f->path_len;

    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".cpy.", 5) == 0) {
            return -1;
        }
    }

    if (f->kind == MP_PACK_KIND_PYC) {
        return -1;
    }

    if (f->kind == MP_PACK_KIND_PY) {
        return 1;
    }

    if (f->kind != MP_PACK_KIND_MPY) {
        return -1;
    }

    #if MICROPY_PERSISTENT_CODE_LOAD
    const uint8_t *data;
    uint32_t data_len;
    uint8_t *to_free = NULL;
    if (!mp_pack_manifest_file_bytes(f, &data, &data_len, &to_free)) {
        return -1;
    }
    if (data_len < 4 || data[0] != 'M' || data[1] != MPY_VERSION) {
        MICROPY_WASM_FREE(to_free);
        return -1;
    }
    // Native arch in feature byte: only accept bytecode (arch == 0) for now.
    if (MPY_FEATURE_DECODE_ARCH(data[2]) != MP_NATIVE_ARCH_NONE) {
        MICROPY_WASM_FREE(to_free);
        return -1;
    }
    if (data[3] > MP_SMALL_INT_BITS) {
        MICROPY_WASM_FREE(to_free);
        return -1;
    }
    int sib_byte = (int)data[3];
    MICROPY_WASM_FREE(to_free);

    // Tagged: ….upy.mpy6.sib31.mpy — prefer highest sib that still fits host.
    const char *tag = NULL;
    size_t tag_len = 0;
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".upy.", 5) == 0) {
            tag = path + i + 5;
            tag_len = path_len - (i + 5);
            break;
        }
    }
    if (tag != NULL && tag_len >= 8 && memcmp(tag, "mpy", 3) == 0) {
        unsigned mpy_ver = 0;
        size_t p = 3;
        while (p < tag_len && tag[p] >= '0' && tag[p] <= '9') {
            mpy_ver = mpy_ver * 10u + (unsigned)(tag[p] - '0');
            ++p;
        }
        const char *sibp = NULL;
        for (size_t j = 0; j + 4 < tag_len; ++j) {
            if (tag[j] == '.' && j + 4 < tag_len && memcmp(tag + j, ".sib", 4) == 0) {
                sibp = tag + j + 4;
                break;
            }
        }
        unsigned sib = 0;
        if (sibp != NULL) {
            while (*sibp >= '0' && *sibp <= '9') {
                sib = sib * 10u + (unsigned)(*sibp - '0');
                ++sibp;
            }
        }
        if (mpy_ver != MPY_VERSION || sib == 0 || sib > (unsigned)MP_SMALL_INT_BITS) {
            return -1;
        }
        return 100 + (int)sib;
    }
    // Legacy untagged .mpy
    return 50 + sib_byte;
    #else
    (void)f;
    return -1;
    #endif
}

static void exec_py_into_module(mp_obj_t module_obj, const char *src_name, const uint8_t *data, uint32_t len) {
    mp_obj_dict_t *globals = mp_obj_module_get_globals(module_obj);
    mp_lexer_t *lex = mp_lexer_new_from_str_len(qstr_from_str(src_name), (const char *)data, len, 0);
    mp_parse_compile_execute(lex, MP_PARSE_FILE_INPUT, globals, globals);
}

#if MICROPY_PERSISTENT_CODE_LOAD
static void exec_mpy_into_module(mp_obj_t module_obj, const char *src_name, const uint8_t *data, uint32_t len) {
    mp_module_context_t *context = (mp_module_context_t *)MP_OBJ_TO_PTR(module_obj);
    mp_compiled_module_t cm;
    cm.context = context;
    mp_raw_code_load_mem(data, len, &cm);

    #if MICROPY_MODULE___FILE__
    mp_store_attr(module_obj, MP_QSTR___file__, MP_OBJ_NEW_QSTR(qstr_from_str(src_name)));
    #else
    (void)src_name;
    #endif

    mp_obj_dict_t *mod_globals = context->module.globals;
    nlr_jump_callback_node_globals_locals_t ctx;
    ctx.globals = mp_globals_get();
    ctx.locals = mp_locals_get();
    mp_globals_set(mod_globals);
    mp_locals_set(mod_globals);
    nlr_push_jump_callback(&ctx.callback, mp_globals_locals_set_from_nlr_jump_callback);
    mp_obj_t module_fun = mp_make_function_from_proto_fun(cm.rc, context, NULL);
    mp_call_function_0(module_fun);
    nlr_pop_jump_callback(true);
}
#endif

static void exec_pack_file_into_module(mp_obj_t module_obj, const char *src_name, const mp_pack_manifest_file_t *f) {
    const uint8_t *data;
    uint32_t data_len;
    uint8_t *to_free = NULL;
    if (!mp_pack_manifest_file_bytes(f, &data, &data_len, &to_free)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm pack file inflate failed"));
    }
    if (f->kind == MP_PACK_KIND_PY) {
        exec_py_into_module(module_obj, src_name, data, data_len);
        MICROPY_WASM_FREE(to_free);
        return;
    }
    #if MICROPY_PERSISTENT_CODE_LOAD
    if (f->kind == MP_PACK_KIND_MPY) {
        exec_mpy_into_module(module_obj, src_name, data, data_len);
        MICROPY_WASM_FREE(to_free);
        return;
    }
    #endif
    MICROPY_WASM_FREE(to_free);
    mp_raise_msg_varg(&mp_type_ValueError,
        MP_ERROR_TEXT("wasm pack file kind %d not supported"), (int)f->kind);
}

static void ensure_parent_packages(const char *full_name) {
    size_t len = strlen(full_name);
    for (size_t i = 0; i < len; ++i) {
        if (full_name[i] != '.') {
            continue;
        }
        qstr parent = qstr_from_strn(full_name, i);
        mp_obj_t pmod = mp_obj_new_module(parent);
        // MicroPython packages use a str __path__ (not a list like CPython).
        mp_map_elem_t *el = mp_map_lookup(&mp_obj_module_get_globals(pmod)->map,
            MP_OBJ_NEW_QSTR(MP_QSTR___path__), MP_MAP_LOOKUP);
        if (el == NULL) {
            mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(pmod)),
                MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                mp_obj_new_str(qstr_str(parent), i));
        }
    }
}

static void link_module_to_parent(const char *dotted_name) {
    const char *dot = strrchr(dotted_name, '.');
    if (dot == NULL) {
        return;
    }
    qstr qparent = qstr_from_strn(dotted_name, (size_t)(dot - dotted_name));
    qstr qleaf = qstr_from_str(dot + 1);
    mp_map_elem_t *pel = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qparent), MP_MAP_LOOKUP);
    mp_map_elem_t *cel = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(dotted_name)), MP_MAP_LOOKUP);
    if (pel != NULL && cel != NULL && pel->value != MP_OBJ_NULL && cel->value != MP_OBJ_NULL) {
        mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(pel->value)),
            MP_OBJ_NEW_QSTR(qleaf), cel->value);
    }
}

static bool path_is_package_init(const char *path, size_t path_len) {
    size_t n = pack_logical_path_len(path, path_len);
    return (n == 8 && memcmp(path, "__init__", 8) == 0)
        || (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0);
}

static const char *stem_from_path(const char *path, char *buf, size_t buf_len) {
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    size_t n = strlen(base);
    if (n >= 5 && strcmp(base + n - 5, ".zlib") == 0) {
        n -= 5;
    }
    if (n >= 5 && strcmp(base + n - 5, ".wasm") == 0) {
        n -= 5;
    } else {
        size_t alen = mp_wasm_aot_suffix_len_n(base, n);
        if (alen > 0) {
            n -= alen;
        }
    }
    if (n == 8 && memcmp(base, "__init__", 8) == 0) {
        // package dir name is the parent folder
        const char *slash = NULL;
        for (const char *p = path; p < base; ++p) {
            if (*p == '/' || *p == '\\') {
                slash = p;
            }
        }
        if (slash != NULL) {
            const char *start = path;
            for (const char *p = path; p < slash; ++p) {
                if (*p == '/' || *p == '\\') {
                    start = p + 1;
                }
            }
            n = (size_t)(slash - start);
            if (n >= buf_len) {
                n = buf_len - 1;
            }
            memcpy(buf, start, n);
            buf[n] = '\0';
            return buf;
        }
    }
    if (n >= buf_len) {
        n = buf_len - 1;
    }
    memcpy(buf, base, n);
    buf[n] = '\0';
    return buf;
}

static mp_obj_t module_for_export_suffix(const char *pack_name, const char *suffix, uint16_t suffix_len) {
    if (suffix_len == 0 || (suffix_len == 1 && suffix[0] == '.')) {
        return mp_obj_new_module(qstr_from_str(pack_name));
    }
    vstr_t dotted;
    vstr_init(&dotted, strlen(pack_name) + suffix_len + 2);
    vstr_add_str(&dotted, pack_name);
    vstr_add_char(&dotted, '.');
    vstr_add_strn(&dotted, suffix, suffix_len);
    const char *name = vstr_null_terminated_str(&dotted);
    ensure_parent_packages(name);
    mp_obj_t mod = mp_obj_new_module(qstr_from_str(name));
    vstr_clear(&dotted);
    return mod;
}

static void bind_pack_exports(mp_obj_t root, mp_pack_t *wmod, const char *pack_name, const mp_pack_manifest_t *info) {
    if (info != NULL && info->n_exports > 0) {
        for (uint32_t i = 0; i < info->n_exports; ++i) {
            const mp_pack_manifest_export_t *ex = &info->exports[i];
            if (ex->func_len == 0 || ex->export_len == 0) {
                continue;
            }
            vstr_t ename;
            vstr_init(&ename, ex->export_len + 1);
            vstr_add_strn(&ename, ex->export_name, ex->export_len);
            const char *export_c = vstr_null_terminated_str(&ename);
            // Introspect Wasm types (sig tag is a hint only; numeric i32/i64/f32/f64).
            if (!mp_pack_numeric_export_arity(wmod, export_c, NULL, NULL)) {
                vstr_clear(&ename);
                continue;
            }
            qstr qexport = qstr_from_strn(ename.buf, ename.len);
            vstr_clear(&ename);

            mp_obj_t target = module_for_export_suffix(pack_name, ex->module, ex->module_len);
            mp_obj_t f = mp_wasm_func_new(wmod, qexport);
            mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(target)),
                MP_OBJ_NEW_QSTR(qstr_from_strn(ex->func, ex->func_len)), f);
            (void)root;
        }
        return;
    }
    // No export table: bind all numeric exports on the pack root.
    bind_ctx_t bctx = { .py_mod = root, .wasm = wmod };
    mp_pack_foreach_numeric_export(wmod, bind_export_cb, &bctx);
}

// Bind Python package + call mp_pack_load for an already-instantiated module.
static mp_obj_t finish_pack_after_load(mp_pack_t *wmod, const uint8_t *meta, uint32_t meta_len,
    const char *pack_name) {
    int32_t lc = 0;
    (void)mp_pack_call0(wmod, "mp_pack_load", &lc, NULL, 0);

    qstr qpack = qstr_from_str(pack_name);
    mp_obj_t root = mp_obj_new_module(qpack);
    mp_obj_t wasm_obj = mp_wasm_wrap_loaded(wmod);
    ((mp_obj_pack_module_t *)MP_OBJ_TO_PTR(wasm_obj))->pack_name = qpack;

    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(root)),
        MP_OBJ_NEW_QSTR(MP_QSTR___pack__), wasm_obj);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(root)),
        MP_OBJ_NEW_QSTR(MP_QSTR___path__),
        mp_obj_new_str(pack_name, strlen(pack_name)));

    mp_pack_manifest_t info;
    memset(&info, 0, sizeof(info));
    bool have_pack = false;
    {
        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        have_pack = mp_pack_manifest_find_section(meta, meta_len, &payload, &payload_len)
            && mp_pack_manifest_parse(payload, payload_len, &info);
    }
    bind_pack_exports(root, wmod, pack_name, have_pack ? &info : NULL);

    if (have_pack) {
        uint32_t *best_idx = m_new(uint32_t, info.n_files ? info.n_files : 1);
        int *best_score = m_new(int, info.n_files ? info.n_files : 1);
        uint32_t n_best = 0;
        for (uint32_t i = 0; i < info.n_files; ++i) {
            const mp_pack_manifest_file_t *f = &info.files[i];
            if (f->kind != MP_PACK_KIND_PY && f->kind != MP_PACK_KIND_MPY
                && f->kind != MP_PACK_KIND_PYC) {
                continue;
            }
            int score = score_pack_file_for_upy_host(f);
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
            vstr_t dotted;
            vstr_init(&dotted, f->path_len + 16);
            path_to_dotted(pack_name, strlen(pack_name), f->path, f->path_len, &dotted);
            const char *dotted_name = vstr_null_terminated_str(&dotted);
            ensure_parent_packages(dotted_name);
            qstr qmod = qstr_from_str(dotted_name);
            mp_obj_t mod = mp_obj_new_module(qmod);
            mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
                MP_OBJ_NEW_QSTR(MP_QSTR___pack__), wasm_obj);
            if (path_is_package_init(f->path, f->path_len)) {
                mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
                    MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                    mp_obj_new_str(dotted_name, dotted.len));
            }
            vstr_t src_name;
            vstr_init(&src_name, f->path_len + 16);
            vstr_add_str(&src_name, pack_name);
            vstr_add_char(&src_name, ':');
            vstr_add_strn(&src_name, f->path, f->path_len);
            exec_pack_file_into_module(mod, vstr_null_terminated_str(&src_name), f);
            link_module_to_parent(dotted_name);
            vstr_clear(&src_name);
            vstr_clear(&dotted);
        }
        m_del(uint32_t, best_idx, info.n_files ? info.n_files : 1);
        m_del(int, best_score, info.n_files ? info.n_files : 1);
    }
    mp_pack_manifest_free(&info);
    return root;
}

static mp_obj_t load_closure_from_root(const char *root_name, const char *root_version,
    const uint8_t *root_bytes, uint32_t root_len) {
    mp_wasm_closure_t cl;
    char err[160];
    if (!mp_wasm_resolve_closure(root_name, root_version, root_bytes, root_len, &cl, err, sizeof(err))) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm resolve: %s"), err);
    }

    mp_pack_t *mods[MP_WASM_CLOSURE_MAX];
    memset(mods, 0, sizeof(mods));

    // Phase: instantiate + registry_add (inside module_load_ex) for every node.
    for (uint32_t i = 0; i < cl.n_nodes; ++i) {
        uint32_t blen = 0;
        const uint8_t *bytes = mp_wasm_closure_cached(cl.nodes[i].name, cl.nodes[i].version, &blen);
        if (bytes == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm resolve: missing cache entry"));
        }
        mods[i] = mp_pack_load_ex(bytes, blen, NULL, 0, cl.nodes[i].name, NULL, err, sizeof(err));
        if (mods[i] == NULL) {
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm load: %s"), err);
        }
        mp_pack_set_name(mods[i], cl.nodes[i].name);
    }

    // Phase: connect — every guest import target must be registered.
    for (uint32_t i = 0; i < cl.n_nodes; ++i) {
        uint32_t blen = 0;
        const uint8_t *bytes = mp_wasm_closure_cached(cl.nodes[i].name, cl.nodes[i].version, &blen);
        if (!mp_wasm_connect_imports(bytes, blen, err, sizeof(err))) {
            if (mp_wasm_cdn_require_explicit_deps()) {
                mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm connect: %s"), err);
            }
        }
    }

    // Phase: run lifecycle + Python bind (deps-first order from resolve).
    mp_obj_t root_obj = mp_const_none;
    for (uint32_t i = 0; i < cl.n_nodes; ++i) {
        uint32_t blen = 0;
        const uint8_t *bytes = mp_wasm_closure_cached(cl.nodes[i].name, cl.nodes[i].version, &blen);
        mp_obj_t obj = finish_pack_after_load(mods[i], bytes, blen, cl.nodes[i].name);
        if (strcmp(cl.nodes[i].name, root_name) == 0 || root_obj == mp_const_none) {
            root_obj = obj;
        }
    }
    return root_obj;
}

static mp_obj_t load_pack_from_parts(const uint8_t *code, uint32_t code_len,
    const uint8_t *meta, uint32_t meta_len, const char *path_hint, const char *name_override) {
    uint8_t *code_owned = NULL;
    uint8_t *meta_owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&code, &code_len, &code_owned)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm artifact zlib unwrap failed"));
    }
    if (meta != NULL) {
        if (!mp_wasm_artifact_unwrap_zlib(&meta, &meta_len, &meta_owned)) {
            MICROPY_WASM_FREE(code_owned);
            mp_raise_ValueError(MP_ERROR_TEXT("wasm artifact zlib unwrap failed"));
        }
    } else {
        meta = code;
        meta_len = code_len;
    }
    char name_buf[MP_WASM_NAME_MAX + 1];
    const char *pack_name = name_override;
    {
        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        mp_pack_manifest_t peek;
        memset(&peek, 0, sizeof(peek));
        if (pack_name == NULL
            && mp_pack_manifest_find_section(meta, meta_len, &payload, &payload_len)
            && mp_pack_manifest_parse(payload, payload_len, &peek)
            && peek.name_len > 0) {
            size_t n = peek.name_len < sizeof(name_buf) - 1 ? peek.name_len : sizeof(name_buf) - 1;
            memcpy(name_buf, peek.name, n);
            name_buf[n] = '\0';
            pack_name = name_buf;
            mp_pack_manifest_free(&peek);
        } else {
            mp_pack_manifest_free(&peek);
        }
    }
    if (pack_name == NULL && path_hint != NULL) {
        pack_name = stem_from_path(path_hint, name_buf, sizeof(name_buf));
    }
    if (pack_name == NULL || pack_name[0] == '\0') {
        pack_name = "wasm_pack";
    }

    // Closure load when MPWD present or metal-cdn driver active.
    {
        const uint8_t *dp = NULL;
        uint32_t dl = 0;
        bool has_deps = mp_wasm_deps_find_section(meta, meta_len, &dp, &dl);
        if (has_deps || mp_wasm_cdn_require_explicit_deps()) {
            mp_obj_t root = load_closure_from_root(pack_name, "", code, code_len);
            MICROPY_WASM_FREE(code_owned);
            MICROPY_WASM_FREE(meta_owned);
            return root;
        }
    }

    char err[128];
    mp_pack_t *wmod = mp_pack_load_ex(code, code_len, meta, meta_len,
        pack_name, path_hint, err, sizeof(err));
    if (wmod == NULL) {
        MICROPY_WASM_FREE(code_owned);
        MICROPY_WASM_FREE(meta_owned);
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm load: %s"), err);
    }
    mp_pack_set_name(wmod, pack_name);

    {
        uint32_t blen = 0;
        const uint8_t *bytes = mp_pack_meta_bytes(wmod, &blen);
        char cerr[128];
        if (!mp_wasm_connect_imports(bytes, blen, cerr, sizeof(cerr))) {
            if (mp_wasm_cdn_require_explicit_deps()) {
                MICROPY_WASM_FREE(code_owned);
                MICROPY_WASM_FREE(meta_owned);
                mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm connect: %s"), cerr);
            }
        }
        mp_obj_t root = finish_pack_after_load(wmod, bytes, blen, pack_name);
        MICROPY_WASM_FREE(code_owned);
        MICROPY_WASM_FREE(meta_owned);
        return root;
    }
}


static void wasm_path_append_unique(const char *root);

// Flat-mirror deps: load_pack("packs/bridge.wasm") must resolve hello.wasm next to it.
// Append the containing directory to wasm.path (idempotent) before closure fetch.
static void wasm_path_add_local_pack_dir(const char *path) {
    if (path == NULL || path[0] == '\0' || mp_wasm_uri_is_http(path)) {
        return;
    }
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        wasm_path_append_unique(".");
        return;
    }
    if (slash == path) {
        wasm_path_append_unique("/");
        return;
    }
    vstr_t dir;
    vstr_init(&dir, (size_t)(slash - path) + 1);
    vstr_add_strn(&dir, path, (size_t)(slash - path));
    wasm_path_append_unique(vstr_null_terminated_str(&dir));
    vstr_clear(&dir);
}

mp_obj_t mp_pack_load_path(const char *path, const char *name_override) {
    char err[128];
    vstr_t code;
    vstr_t meta;
    bool have_meta = false;
    vstr_t verify_path_storage;
    const char *verify_path = path;
    bool verify_path_owned = false;
    vstr_t logical_storage;
    const char *logical = logical_artifact_path(path, &logical_storage);
    bool logical_owned = (logical != path);

    // Before MPWD closure fetch: sibling packs live beside this artifact.
    wasm_path_add_local_pack_dir(logical);

    #if MICROPY_PY_WASM_AOT
    // Finder may hand us .aotN; otherwise prefer sibling .aotN next to .wasm.
    // Paths may be *.zlib; branching uses the logical (.wasm/.aotN) name.
    if (mp_wasm_path_is_aot(logical)) {
        if (!fetch_artifact(path, &code, err, sizeof(err))) {
            if (logical_owned) {
                vstr_clear(&logical_storage);
            }
            mp_raise_OSError_with_filename(MP_ENOENT, path);
        }
        vstr_t sib;
        if (replace_aot_suffix(logical, ".wasm", &sib)) {
            if (fetch_artifact(vstr_null_terminated_str(&sib), &meta, err, sizeof(err))) {
                have_meta = true;
            }
            vstr_clear(&sib);
        }
        if (!have_meta && replace_aot_suffix(logical, ".mpack", &sib)) {
            if (fetch_artifact(vstr_null_terminated_str(&sib), &meta, err, sizeof(err))) {
                have_meta = true;
            }
            vstr_clear(&sib);
        }
    } else if (ends_with(logical, ".wasm")) {
        char aot_ext[16];
        mp_wasm_aot_format_ext(aot_ext, sizeof(aot_ext));
        vstr_t aot_path;
        bool got_aot = false;
        if (replace_suffix(logical, ".wasm", aot_ext, &aot_path)) {
            if (fetch_artifact(vstr_null_terminated_str(&aot_path), &code, err, sizeof(err))) {
                got_aot = true;
            } else {
                vstr_clear(&aot_path);
            }
        }
        // Legacy sibling .aot when format-tagged name missing.
        if (!got_aot && strcmp(aot_ext, ".aot") != 0
            && replace_suffix(logical, ".wasm", ".aot", &aot_path)) {
            if (fetch_artifact(vstr_null_terminated_str(&aot_path), &code, err, sizeof(err))) {
                got_aot = true;
            } else {
                vstr_clear(&aot_path);
            }
        }
        if (got_aot) {
            // Execute AOT; keep .wasm bytes as metadata.
            if (!fetch_artifact(logical, &meta, err, sizeof(err))) {
                vstr_clear(&code);
                vstr_clear(&aot_path);
                if (logical_owned) {
                    vstr_clear(&logical_storage);
                }
                mp_raise_OSError_with_filename(MP_ENOENT, path);
            }
            have_meta = true;
            vstr_init(&verify_path_storage, aot_path.len + 1);
            vstr_add_strn(&verify_path_storage, aot_path.buf, aot_path.len);
            verify_path = vstr_null_terminated_str(&verify_path_storage);
            verify_path_owned = true;
            vstr_clear(&aot_path);
        } else {
            if (!fetch_artifact(path, &code, err, sizeof(err))) {
                if (logical_owned) {
                    vstr_clear(&logical_storage);
                }
                mp_raise_OSError_with_filename(MP_ENOENT, path);
            }
        }
    } else
    #endif
    {
        if (!fetch_artifact(path, &code, err, sizeof(err))) {
            if (logical_owned) {
                vstr_clear(&logical_storage);
            }
            mp_raise_OSError_with_filename(MP_ENOENT, path);
        }
    }

    mp_obj_t root = load_pack_from_parts(
        (const uint8_t *)code.buf, (uint32_t)code.len,
        have_meta ? (const uint8_t *)meta.buf : NULL,
        have_meta ? (uint32_t)meta.len : 0,
        verify_path, name_override);
    vstr_clear(&code);
    if (have_meta) {
        vstr_clear(&meta);
    }
    if (verify_path_owned) {
        vstr_clear(&verify_path_storage);
    }
    if (logical_owned) {
        vstr_clear(&logical_storage);
    }
    return root;
}

mp_obj_t mp_pack_load_bytes(const uint8_t *code, uint32_t code_len, const char *name_override) {
    return mp_pack_load_bytes_at(code, code_len, name_override, NULL);
}

mp_obj_t mp_pack_load_bytes_at(const uint8_t *code, uint32_t code_len,
    const char *name_override, const char *origin) {
    if (code == NULL || code_len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("empty wasm pack bytes"));
    }
    return load_pack_from_parts(code, code_len, NULL, 0, origin, name_override);
}

static mp_obj_t mod_wasm_load_pack(size_t n_args, const mp_obj_t *args) {
    const char *name_override = NULL;
    if (n_args >= 2 && args[1] != mp_const_none) {
        name_override = mp_obj_str_get_str(args[1]);
    }
    if (mp_obj_is_str(args[0])) {
        return mp_pack_load_path(mp_obj_str_get_str(args[0]), name_override);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    return load_pack_from_parts(bufinfo.buf, (uint32_t)bufinfo.len, NULL, 0, NULL, name_override);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_load_pack_obj, 1, 2, mod_wasm_load_pack);

static mp_obj_t mod_wasm_unload(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    size_t nlen = strlen(name);
    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;

    mp_map_elem_t *el = mp_map_lookup(map, MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        mp_map_elem_t *w = mp_map_lookup(&mp_obj_module_get_globals(el->value)->map,
            MP_OBJ_NEW_QSTR(MP_QSTR___pack__), MP_MAP_LOOKUP);
        if (w != NULL && mp_obj_is_type(w->value, (mp_obj_type_t *)&mp_type_pack_module)) {
            mp_obj_pack_module_t *wo = MP_OBJ_TO_PTR(w->value);
            if (wo->mod) {
                int32_t lc = 0;
                (void)mp_pack_call0(wo->mod, "mp_pack_unload", &lc, NULL, 0);
            }
            wasm_module_close(MP_OBJ_FROM_PTR(wo));
        }
    }

    size_t nrem = 0;
    mp_obj_t *keys = m_new(mp_obj_t, map->alloc);
    for (size_t i = 0; i < map->alloc; ++i) {
        if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_qstr(map->table[i].key)) {
            continue;
        }
        const char *k = qstr_str(MP_OBJ_QSTR_VALUE(map->table[i].key));
        size_t klen = strlen(k);
        if ((klen == nlen && memcmp(k, name, nlen) == 0)
            || (klen > nlen && k[nlen] == '.' && memcmp(k, name, nlen) == 0)) {
            keys[nrem++] = map->table[i].key;
        }
    }
    for (size_t i = 0; i < nrem; ++i) {
        mp_map_lookup(map, keys[i], MP_MAP_LOOKUP_REMOVE_IF_FOUND);
    }
    m_del(mp_obj_t, keys, map->alloc);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_unload_obj, mod_wasm_unload);

static mp_obj_t mod_wasm_import_wasm(mp_obj_t name_in) {
    return mp_wasm_import_wasm(mp_obj_str_get_str(name_in));
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_import_wasm_obj, mod_wasm_import_wasm);

static mp_obj_t mod_wasm_import_hook(size_t n_args, const mp_obj_t *args) {
    mp_obj_t prev = MP_STATE_VM(mp_wasm_prev_import);
    if (prev == MP_OBJ_NULL) {
        prev = MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    }

    // Fast path: already loaded — do not probe the filesystem.
    if (mp_wasm_import_hook_depth == 0 && n_args >= 1 && mp_obj_is_str(args[0])) {
        const char *name = mp_obj_str_get_str(args[0]);
        mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
            MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
        if (el != NULL && el->value != MP_OBJ_NULL) {
            return mp_call_function_n_kw(prev, n_args, 0, args);
        }

        // Prefer packs on wasm.path (VFS or HTTP) over a same-named empty directory
        // on sys.path — but never when metal-cdn is active (artifacts/ only).
        vstr_t path;
        if (mp_wasm_cdn_driver() != MP_WASM_CDN_DRIVER_METAL
            && mp_wasm_find_pack_on_wasm_path(name, &path)) {
            mp_wasm_import_hook_depth++;
            nlr_buf_t nlr_pack;
            if (nlr_push(&nlr_pack) == 0) {
                // Reuse the path already probed — do not find_pack again.
                (void)mp_wasm_import_wasm_at(name, vstr_null_terminated_str(&path));
                nlr_pop();
                mp_wasm_import_hook_depth--;
                vstr_clear(&path);
            } else {
                mp_wasm_import_hook_depth--;
                vstr_clear(&path);
                nlr_jump(nlr_pack.ret_val);
            }
        }
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_n_kw(prev, n_args, 0, args);
        nlr_pop();
        return res;
    }

    mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
    if (!mp_obj_exception_match(exc, MP_OBJ_FROM_PTR(&mp_type_ImportError)) || mp_wasm_import_hook_depth > 0) {
        nlr_jump(nlr.ret_val);
    }

    // Fallback: leaf pack missed above, or namespace-only (descendants / listdir).
    const char *name = mp_obj_str_get_str(args[0]);
    mp_wasm_import_hook_depth++;
    nlr_buf_t nlr2;
    if (nlr_push(&nlr2) == 0) {
        (void)mp_wasm_import_wasm(name);
        nlr_pop();
        mp_wasm_import_hook_depth--;
        return mp_call_function_n_kw(prev, n_args, 0, args);
    }
    mp_wasm_import_hook_depth--;
    // Prefer the original ImportError when the finder misses.
    if (mp_obj_exception_match(MP_OBJ_FROM_PTR(nlr2.ret_val), MP_OBJ_FROM_PTR(&mp_type_ImportError))) {
        nlr_jump(nlr.ret_val);
    }
    nlr_jump(nlr2.ret_val);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_import_hook_obj, 1, 5, mod_wasm_import_hook);

static void wasm_path_append_unique(const char *root) {
    mp_wasm_path_ensure();
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_wasm_path_obj)), &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (!mp_obj_is_str(items[i])) {
            continue;
        }
        const char *existing = mp_obj_str_get_str(items[i]);
        if (strcmp(existing, root) == 0) {
            return;
        }
        // Treat "http://h/packs" and "http://h/packs/" as the same root.
        size_t elen = strlen(existing);
        size_t rlen = strlen(root);
        if (elen > 0 && existing[elen - 1] == '/' && elen == rlen + 1
            && strncmp(existing, root, rlen) == 0) {
            return;
        }
        if (rlen > 0 && root[rlen - 1] == '/' && rlen == elen + 1
            && strncmp(root, existing, elen) == 0) {
            return;
        }
    }
    mp_obj_list_append(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_wasm_path_obj)),
        mp_obj_new_str(root, strlen(root)));
}


static void wasm_path_prepend_unique(const char *root) {
    mp_wasm_path_ensure();
    mp_obj_list_t *list = &MP_STATE_VM(mp_wasm_path_obj);
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(MP_OBJ_FROM_PTR(list), &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (!mp_obj_is_str(items[i])) {
            continue;
        }
        const char *existing = mp_obj_str_get_str(items[i]);
        if (strcmp(existing, root) == 0) {
            return;
        }
        size_t elen = strlen(existing);
        size_t rlen = strlen(root);
        if (elen > 0 && existing[elen - 1] == '/' && elen == rlen + 1
            && strncmp(existing, root, rlen) == 0) {
            return;
        }
        if (rlen > 0 && root[rlen - 1] == '/' && rlen == elen + 1
            && strncmp(root, existing, elen) == 0) {
            return;
        }
    }
    // Insert at front: earlier roots win (PyPI-style index priority).
    mp_obj_t root_obj = mp_obj_new_str(root, strlen(root));
    mp_obj_list_append(MP_OBJ_FROM_PTR(list), mp_const_none);
    mp_obj_list_get(MP_OBJ_FROM_PTR(list), &n, &items);
    for (size_t i = n; i > 1; --i) {
        items[i - 1] = items[i - 2];
    }
    items[0] = root_obj;
}

static void wasm_path_add_http_root(const char *url, bool prepend) {
    if (!mp_wasm_uri_is_http(url)) {
        mp_raise_ValueError(MP_ERROR_TEXT("install_hook: url must be http(s)"));
    }
    size_t len = strlen(url);
    vstr_t root;
    vstr_init(&root, len + 2);
    vstr_add_strn(&root, url, len);
    if (root.len == 0 || root.buf[root.len - 1] != '/') {
        vstr_add_char(&root, '/');
    }
    if (prepend) {
        wasm_path_prepend_unique(vstr_null_terminated_str(&root));
    } else {
        wasm_path_append_unique(vstr_null_terminated_str(&root));
    }
    vstr_clear(&root);
}

static void wasm_path_add_http_roots(mp_obj_t url_obj) {
    // Flat HTTP pack mirrors on wasm.path only. Metal-cdn is wasm.cdn(base).
    if (mp_obj_is_str(url_obj)) {
        wasm_path_add_http_root(mp_obj_str_get_str(url_obj), true);
        return;
    }
    size_t n = 0;
    mp_obj_t *items = NULL;
    mp_obj_get_array(url_obj, &n, &items);
    if (n == 0) {
        return;
    }
    // Prepend in reverse so the first list entry ends up first on wasm.path.
    for (size_t i = n; i > 0; --i) {
        if (!mp_obj_is_str(items[i - 1])) {
            mp_raise_TypeError(MP_ERROR_TEXT("install_hook: url entries must be str"));
        }
        wasm_path_add_http_root(mp_obj_str_get_str(items[i - 1]), true);
    }
}


static mp_obj_t mod_wasm_install_hook(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_url };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_url, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_url].u_obj != mp_const_none) {
        wasm_path_add_http_roots(args[ARG_url].u_obj);
    }

    #if !MICROPY_CAN_OVERRIDE_BUILTINS
    mp_raise_NotImplementedError(MP_ERROR_TEXT("wasm.install_hook requires MICROPY_CAN_OVERRIDE_BUILTINS"));
    #else
    if (MP_STATE_VM(mp_wasm_prev_import) != MP_OBJ_NULL) {
        return mp_const_none;
    }
    mp_obj_t dest[2] = { MP_OBJ_NULL, MP_OBJ_NULL };
    mp_load_method_maybe(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, dest);
    MP_STATE_VM(mp_wasm_prev_import) =
        dest[0] != MP_OBJ_NULL ? dest[0] : MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, MP_OBJ_FROM_PTR(&mod_wasm_import_hook_obj));
    return mp_const_none;
    #endif
}
MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_install_hook_obj, 0, mod_wasm_install_hook);

// wasm.cdn(url, token=None) → driver name string
static mp_obj_t mod_wasm_cdn(size_t n_args, const mp_obj_t *args) {
    if (n_args < 1 || !mp_obj_is_str(args[0])) {
        mp_raise_TypeError(MP_ERROR_TEXT("cdn(url, token=None)"));
    }
    const char *url = mp_obj_str_get_str(args[0]);
    if (!mp_wasm_uri_is_http(url)) {
        mp_raise_ValueError(MP_ERROR_TEXT("cdn: url must be http(s)"));
    }
    const char *token = NULL;
    if (n_args >= 2 && args[1] != mp_const_none) {
        if (!mp_obj_is_str(args[1])) {
            mp_raise_TypeError(MP_ERROR_TEXT("cdn: token must be str"));
        }
        token = mp_obj_str_get_str(args[1]);
    }
    mp_wasm_cdn_configure(url, token);
    // Metal uses /artifacts/… only — drop this base from wasm.path if present
    // (avoids flat probes on {base}/<name>.wasm that can 200 non-pack bodies).
    mp_wasm_path_ensure();
    mp_obj_list_t *list = &MP_STATE_VM(mp_wasm_path_obj);
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(MP_OBJ_FROM_PTR(list), &n, &items);
    for (size_t i = n; i > 0; --i) {
        size_t idx = i - 1;
        if (!mp_obj_is_str(items[idx])) {
            continue;
        }
        if (mp_wasm_cdn_url_is_base(mp_obj_str_get_str(items[idx]))) {
            mp_obj_list_remove(MP_OBJ_FROM_PTR(list), items[idx]);
        }
    }
    const char *name = mp_wasm_cdn_driver_name();
    return mp_obj_new_str(name, strlen(name));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_cdn_obj, 1, 2, mod_wasm_cdn);

// wasm.catalog(channel="lead") → list of package name strings
static mp_obj_t mod_wasm_catalog(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_channel };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_channel, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *channel = "lead";
    if (args[ARG_channel].u_obj != mp_const_none) {
        if (!mp_obj_is_str(args[ARG_channel].u_obj)) {
            mp_raise_TypeError(MP_ERROR_TEXT("catalog: channel must be str"));
        }
        channel = mp_obj_str_get_str(args[ARG_channel].u_obj);
    }

    char err[160];
    uint8_t *buf = NULL;
    uint32_t len = 0;
    if (!mp_wasm_cdn_fetch_index(channel, &buf, &len, err, sizeof(err))) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), err);
    }

    // Parse JSON via json.loads when available.
    mp_obj_t json_mod = mp_import_name(MP_QSTR_json, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t loads = mp_load_attr(json_mod, MP_QSTR_loads);
    mp_obj_t text = mp_obj_new_str((const char *)buf, len);
    MICROPY_WASM_FREE(buf);
    mp_obj_t doc = mp_call_function_1(loads, text);
    if (!mp_obj_is_dict_or_ordereddict(doc)) {
        mp_raise_ValueError(MP_ERROR_TEXT("catalog: index JSON root must be object"));
    }
    mp_obj_t packages = mp_obj_dict_get(doc, MP_OBJ_NEW_QSTR(MP_QSTR_packages));
    if (!mp_obj_is_dict_or_ordereddict(packages)) {
        mp_raise_ValueError(MP_ERROR_TEXT("catalog: missing packages object"));
    }
    mp_map_t *map = mp_obj_dict_get_map(packages);
    mp_obj_t out = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < map->alloc; ++i) {
        if (!mp_map_slot_is_filled(map, i)) {
            continue;
        }
        mp_obj_list_append(out, map->table[i].key);
    }
    return out;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_catalog_obj, 0, mod_wasm_catalog);

// wasm.session_id  get/set via function: wasm.session_id() / wasm.session_id(id)
static mp_obj_t mod_wasm_session_id(size_t n_args, const mp_obj_t *args) {
    if (n_args >= 1) {
        if (args[0] == mp_const_none) {
            mp_wasm_cdn_set_session_id(NULL);
        } else if (mp_obj_is_str(args[0])) {
            mp_wasm_cdn_set_session_id(mp_obj_str_get_str(args[0]));
        } else {
            mp_raise_TypeError(MP_ERROR_TEXT("session_id must be str or None"));
        }
    }
    const char *sid = mp_wasm_cdn_session_id();
    if (sid == NULL || sid[0] == '\0') {
        return mp_const_none;
    }
    return mp_obj_new_str(sid, strlen(sid));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_session_id_obj, 0, 1, mod_wasm_session_id);

// wasm.publish(name, version, data, *, lead=True, pin=True, token=None)
static mp_obj_t mod_wasm_publish(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_name, ARG_version, ARG_data, ARG_lead, ARG_pin, ARG_token };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_name, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_version, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_lead, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_pin, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_token, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (!mp_obj_is_str(args[ARG_name].u_obj) || !mp_obj_is_str(args[ARG_version].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("publish: name and version must be str"));
    }
    const char *name = mp_obj_str_get_str(args[ARG_name].u_obj);
    const char *version = mp_obj_str_get_str(args[ARG_version].u_obj);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_data].u_obj, &bufinfo, MP_BUFFER_READ);
    const char *token = NULL;
    if (args[ARG_token].u_obj != mp_const_none) {
        if (!mp_obj_is_str(args[ARG_token].u_obj)) {
            mp_raise_TypeError(MP_ERROR_TEXT("publish: token must be str"));
        }
        token = mp_obj_str_get_str(args[ARG_token].u_obj);
    }
    char err[160];
    if (!mp_wasm_cdn_publish(name, version, bufinfo.buf, (uint32_t)bufinfo.len,
            args[ARG_lead].u_bool, args[ARG_pin].u_bool, token, err, sizeof(err))) {
        mp_raise_msg_varg(&mp_type_NotImplementedError, MP_ERROR_TEXT("%s"), err);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_publish_obj, 3, mod_wasm_publish);

// wasm.publish_file(path, name, version, *, lead=True, pin=True, token=None)
static mp_obj_t mod_wasm_publish_file(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_path, ARG_name, ARG_version, ARG_lead, ARG_pin, ARG_token };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_name, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_version, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_lead, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_pin, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_token, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (!mp_obj_is_str(args[ARG_path].u_obj)
        || !mp_obj_is_str(args[ARG_name].u_obj)
        || !mp_obj_is_str(args[ARG_version].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("publish_file: path, name, version must be str"));
    }
    const char *path = mp_obj_str_get_str(args[ARG_path].u_obj);
    char err[160];
    vstr_t code;
    if (!mp_wasm_fetch(path, &code, err, sizeof(err))) {
        mp_raise_OSError_with_filename(MP_ENOENT, path);
    }
    const char *token = NULL;
    if (args[ARG_token].u_obj != mp_const_none) {
        if (!mp_obj_is_str(args[ARG_token].u_obj)) {
            vstr_clear(&code);
            mp_raise_TypeError(MP_ERROR_TEXT("publish_file: token must be str"));
        }
        token = mp_obj_str_get_str(args[ARG_token].u_obj);
    }
    bool ok = mp_wasm_cdn_publish(
        mp_obj_str_get_str(args[ARG_name].u_obj),
        mp_obj_str_get_str(args[ARG_version].u_obj),
        (const uint8_t *)code.buf, (uint32_t)code.len,
        args[ARG_lead].u_bool, args[ARG_pin].u_bool, token, err, sizeof(err));
    vstr_clear(&code);
    if (!ok) {
        mp_raise_msg_varg(&mp_type_NotImplementedError, MP_ERROR_TEXT("%s"), err);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_publish_file_obj, 3, mod_wasm_publish_file);

static mp_obj_t mod_wasm_uninstall_hook(void) {
    #if !MICROPY_CAN_OVERRIDE_BUILTINS
    return mp_const_none;
    #else
    if (MP_STATE_VM(mp_wasm_prev_import) == MP_OBJ_NULL) {
        return mp_const_none;
    }
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, MP_STATE_VM(mp_wasm_prev_import));
    MP_STATE_VM(mp_wasm_prev_import) = MP_OBJ_NULL;
    mp_wasm_import_hook_depth = 0;
    mp_wasm_cdn_reset();
    mp_wasm_closure_cache_clear();
    return mp_const_none;
    #endif
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_uninstall_hook_obj, mod_wasm_uninstall_hook);

#endif // MICROPY_PY_WASM
