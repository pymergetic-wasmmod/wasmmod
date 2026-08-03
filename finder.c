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

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include <string.h>

#include "py/builtin.h"
#include "py/runtime.h"

#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/finder.h"

#ifndef MICROPY_PY_WASM_AOT
#define MICROPY_PY_WASM_AOT (0)
#endif

// Defined in wasmmod.c
mp_obj_t mp_wasm_load_pack_path(const char *path, const char *name_override);
void mp_wasm_path_ensure(void);
mp_obj_t mp_wasm_path_obj(void);

static bool path_is_frozen(const char *root) {
    return root != NULL && strcmp(root, ".frozen") == 0;
}

static void dotted_to_slash(const char *dotted, vstr_t *out) {
    vstr_init(out, strlen(dotted) + 1);
    for (const char *p = dotted; *p; ++p) {
        vstr_add_char(out, *p == '.' ? '/' : *p);
    }
}

static bool try_vfs_file(const char *root, const char *rel, vstr_t *path_out) {
    vstr_t path;
    mp_wasm_join_uri(root, rel, &path);
    const char *cpath = vstr_null_terminated_str(&path);
    if (mp_import_stat(cpath) == MP_IMPORT_STAT_FILE) {
        vstr_init(path_out, path.len + 1);
        vstr_add_strn(path_out, path.buf, path.len);
        vstr_clear(&path);
        return true;
    }
    vstr_clear(&path);
    return false;
}

static bool try_url_candidate(const char *root, const char *rel, vstr_t *path_out) {
    // URL roots: record the candidate URI; fetch happens at load time.
    mp_wasm_join_uri(root, rel, path_out);
    return true;
}

static bool find_in_root(const char *root, const char *slash_name, vstr_t *path_out) {
    if (root == NULL || path_is_frozen(root)) {
        return false;
    }
    // sys.path "" means cwd — same as "." for VFS lookups.
    if (root[0] == '\0') {
        root = ".";
    }
    bool url = mp_wasm_uri_is_http(root);

    // HTTP roots: only module-form URLs (load validates existence).
    if (url) {
        vstr_t rel;
        vstr_init(&rel, strlen(slash_name) + 8);
        vstr_add_str(&rel, slash_name);
        #if MICROPY_PY_WASM_AOT
        vstr_add_str(&rel, ".aot");
        #else
        vstr_add_str(&rel, ".wasm");
        #endif
        bool ok = try_url_candidate(root, vstr_null_terminated_str(&rel), path_out);
        vstr_clear(&rel);
        return ok;
    }

    vstr_t rel;
    vstr_init(&rel, strlen(slash_name) + 16);
    #if MICROPY_PY_WASM_AOT
    vstr_add_str(&rel, slash_name);
    vstr_add_str(&rel, "/__init__.aot");
    if (try_vfs_file(root, vstr_null_terminated_str(&rel), path_out)) {
        vstr_clear(&rel);
        return true;
    }
    vstr_clear(&rel);
    vstr_init(&rel, strlen(slash_name) + 16);
    #endif
    // 1) package: name/__init__.wasm
    vstr_add_str(&rel, slash_name);
    vstr_add_str(&rel, "/__init__.wasm");
    if (try_vfs_file(root, vstr_null_terminated_str(&rel), path_out)) {
        vstr_clear(&rel);
        return true;
    }

    // 2) module: name.wasm
    vstr_clear(&rel);
    vstr_init(&rel, strlen(slash_name) + 8);
    #if MICROPY_PY_WASM_AOT
    vstr_add_str(&rel, slash_name);
    vstr_add_str(&rel, ".aot");
    if (try_vfs_file(root, vstr_null_terminated_str(&rel), path_out)) {
        vstr_clear(&rel);
        return true;
    }
    vstr_clear(&rel);
    vstr_init(&rel, strlen(slash_name) + 8);
    #endif
    vstr_add_str(&rel, slash_name);
    vstr_add_str(&rel, ".wasm");
    if (try_vfs_file(root, vstr_null_terminated_str(&rel), path_out)) {
        vstr_clear(&rel);
        return true;
    }

    // 3) pack-dir layout: name/name.wasm (basename == leaf dir)
    {
        const char *leaf = strrchr(slash_name, '/');
        leaf = leaf ? leaf + 1 : slash_name;
        vstr_clear(&rel);
        vstr_init(&rel, strlen(slash_name) + strlen(leaf) + 8);
        #if MICROPY_PY_WASM_AOT
        vstr_add_str(&rel, slash_name);
        vstr_add_char(&rel, '/');
        vstr_add_str(&rel, leaf);
        vstr_add_str(&rel, ".aot");
        if (try_vfs_file(root, vstr_null_terminated_str(&rel), path_out)) {
            vstr_clear(&rel);
            return true;
        }
        vstr_clear(&rel);
        vstr_init(&rel, strlen(slash_name) + strlen(leaf) + 8);
        #endif
        vstr_add_str(&rel, slash_name);
        vstr_add_char(&rel, '/');
        vstr_add_str(&rel, leaf);
        vstr_add_str(&rel, ".wasm");
        bool ok = try_vfs_file(root, vstr_null_terminated_str(&rel), path_out);
        vstr_clear(&rel);
        return ok;
    }
}

static bool find_in_list(mp_obj_t list_obj, const char *slash_name, vstr_t *path_out) {
    if (list_obj == MP_OBJ_NULL || !mp_obj_is_type(list_obj, &mp_type_list)) {
        return false;
    }
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(list_obj, &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (!mp_obj_is_str(items[i])) {
            continue;
        }
        if (find_in_root(mp_obj_str_get_str(items[i]), slash_name, path_out)) {
            return true;
        }
    }
    return false;
}

bool mp_wasm_find_pack(const char *dotted_name, vstr_t *path_out) {
    if (dotted_name == NULL || dotted_name[0] == '\0') {
        return false;
    }
    vstr_t slash;
    dotted_to_slash(dotted_name, &slash);
    const char *slash_name = vstr_null_terminated_str(&slash);

    mp_wasm_path_ensure();
    if (find_in_list(mp_wasm_path_obj(), slash_name, path_out)) {
        vstr_clear(&slash);
        return true;
    }
    #if MICROPY_PY_SYS_PATH
    if (find_in_list(mp_sys_path, slash_name, path_out)) {
        vstr_clear(&slash);
        return true;
    }
    #endif
    vstr_clear(&slash);
    return false;
}

static mp_obj_t lookup_loaded(const char *name) {
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
    if (el == NULL || el->value == MP_OBJ_NULL) {
        return MP_OBJ_NULL;
    }
    return el->value;
}

mp_obj_t mp_wasm_import_wasm(const char *dotted_name) {
    mp_obj_t existing = lookup_loaded(dotted_name);
    if (existing != MP_OBJ_NULL) {
        return existing;
    }

    const char *dot = strrchr(dotted_name, '.');
    if (dot != NULL) {
        size_t plen = (size_t)(dot - dotted_name);
        vstr_t parent;
        vstr_init(&parent, plen + 1);
        vstr_add_strn(&parent, dotted_name, plen);
        const char *parent_name = vstr_null_terminated_str(&parent);
        if (lookup_loaded(parent_name) == MP_OBJ_NULL) {
            (void)mp_wasm_import_wasm(parent_name);
        }
        vstr_clear(&parent);
        existing = lookup_loaded(dotted_name);
        if (existing != MP_OBJ_NULL) {
            return existing;
        }
    }

    vstr_t path;
    if (!mp_wasm_find_pack(dotted_name, &path)) {
        mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("no wasm pack named '%s'"), dotted_name);
    }
    // URL candidates from http roots are optimistic — load/fetch validates.
    mp_obj_t mod = mp_wasm_load_pack_path(vstr_null_terminated_str(&path), NULL);
    vstr_clear(&path);

    existing = lookup_loaded(dotted_name);
    if (existing != MP_OBJ_NULL) {
        return existing;
    }
    return mod;
}

#endif // MICROPY_PY_WASM
