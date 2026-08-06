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

#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/nlr.h"
#include "py/builtin.h"

#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/finder.h"
#include "extmod/wasmmod/runtime.h"
#include "extmod/wasmmod/cdn.h"
#include "extmod/wasmmod/alloc.h"

#if MICROPY_VFS
#include "extmod/vfs.h"
#endif

#ifndef MICROPY_PY_WASM_AOT
#define MICROPY_PY_WASM_AOT (0)
#endif

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#ifndef MICROPY_WASM_AOT_VERSION
#define MICROPY_WASM_AOT_VERSION (0)
#endif

unsigned mp_wasm_aot_format_version(void) {
    #if MICROPY_PY_WASM_AOT
    return (unsigned)MICROPY_WASM_AOT_VERSION;
    #else
    return 0;
    #endif
}

size_t mp_wasm_aot_suffix_len_n(const char *s, size_t n) {
    if (s == NULL || n < 4) {
        return 0;
    }
    size_t j = n;
    while (j > 0 && s[j - 1] >= '0' && s[j - 1] <= '9') {
        j--;
    }
    if (j >= 4 && memcmp(s + j - 4, ".aot", 4) == 0) {
        return n - (j - 4);
    }
    return 0;
}

size_t mp_wasm_aot_suffix_len(const char *s) {
    return s ? mp_wasm_aot_suffix_len_n(s, strlen(s)) : 0;
}

bool mp_wasm_path_is_aot(const char *path) {
    return mp_wasm_aot_suffix_len(path) > 0;
}

size_t mp_wasm_elf_suffix_len_n(const char *s, size_t n) {
    if (s == NULL || n < 4) {
        return 0;
    }
    return memcmp(s + n - 4, ".elf", 4) == 0 ? 4 : 0;
}

size_t mp_wasm_elf_suffix_len(const char *s) {
    return s ? mp_wasm_elf_suffix_len_n(s, strlen(s)) : 0;
}

bool mp_wasm_path_is_elf(const char *path) {
    return mp_wasm_elf_suffix_len(path) > 0;
}

void mp_wasm_aot_format_ext(char *buf, size_t buflen) {
    if (buf == NULL || buflen < 5) {
        return;
    }
    unsigned v = mp_wasm_aot_format_version();
    if (v > 0) {
        snprintf(buf, buflen, ".aot%u", v);
    } else {
        snprintf(buf, buflen, ".aot");
    }
}

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
    vstr_t url;
    mp_wasm_join_uri(root, rel, &url);
    const char *curi = vstr_null_terminated_str(&url);
    bool ok = mp_wasm_http_probe(curi);
    if (ok) {
        vstr_init(path_out, url.len + 1);
        vstr_add_strn(path_out, url.buf, url.len);
    }
    vstr_clear(&url);
    return ok;
}

// Prefer whole-artifact zlib envelope (rel + ".zlib") when present.
static bool try_rel_prefer_zlib(const char *root, const char *rel, bool url, vstr_t *path_out) {
    vstr_t zrel;
    vstr_init(&zrel, strlen(rel) + 6);
    vstr_add_str(&zrel, rel);
    vstr_add_str(&zrel, ".zlib");
    bool ok;
    if (url) {
        ok = try_url_candidate(root, vstr_null_terminated_str(&zrel), path_out);
    } else {
        ok = try_vfs_file(root, vstr_null_terminated_str(&zrel), path_out);
    }
    vstr_clear(&zrel);
    if (ok) {
        return true;
    }
    if (url) {
        return try_url_candidate(root, rel, path_out);
    }
    return try_vfs_file(root, rel, path_out);
}

// Try stem + optional ".<arch>" + ext under root (VFS or URL).
static bool try_stem_ext(const char *root, const char *stem, const char *arch,
    const char *ext, bool url, vstr_t *path_out) {
    vstr_t rel;
    size_t n = strlen(stem) + strlen(ext) + (arch && arch[0] ? strlen(arch) + 1 : 0) + 1;
    vstr_init(&rel, n);
    vstr_add_str(&rel, stem);
    if (arch != NULL && arch[0] != '\0') {
        vstr_add_char(&rel, '.');
        vstr_add_str(&rel, arch);
    }
    vstr_add_str(&rel, ext);
    bool ok = try_rel_prefer_zlib(root, vstr_null_terminated_str(&rel), url, path_out);
    vstr_clear(&rel);
    return ok;
}

// Package form: stem/__init__[.arch].ext
static bool try_pkg_init(const char *root, const char *stem, const char *arch,
    const char *ext, bool url, vstr_t *path_out) {
    vstr_t rel;
    size_t n = strlen(stem) + 8 + strlen(ext) + (arch && arch[0] ? strlen(arch) + 1 : 0) + 2;
    vstr_init(&rel, n);
    vstr_add_str(&rel, stem);
    vstr_add_str(&rel, "/__init__");
    if (arch != NULL && arch[0] != '\0') {
        vstr_add_char(&rel, '.');
        vstr_add_str(&rel, arch);
    }
    vstr_add_str(&rel, ext);
    bool ok = try_rel_prefer_zlib(root, vstr_null_terminated_str(&rel), url, path_out);
    vstr_clear(&rel);
    return ok;
}

// One arch tag (may be "") + one extension: pkg __init__ then module file.
static bool try_one_arch_ext(const char *root, const char *stem, bool allow_pkg,
    const char *arch, const char *ext, bool url, vstr_t *path_out) {
    if (allow_pkg && try_pkg_init(root, stem, arch, ext, url, path_out)) {
        return true;
    }
    return try_stem_ext(root, stem, arch, ext, url, path_out);
}

// Container preference (MICROPY_WASM_CONTAINERS): elf, aot, wasm.
static bool try_aot_variants(const char *root, const char *stem, bool allow_pkg,
    bool url, vstr_t *path_out) {
#if MICROPY_PY_WASM_AOT
    mp_wasm_arch_ensure();
    size_t n_arch = 0;
    mp_obj_t *arch_items = NULL;
    mp_obj_list_get(mp_wasm_arch_obj(), &n_arch, &arch_items);

    char aot_ext[16];
    mp_wasm_aot_format_ext(aot_ext, sizeof(aot_ext));

    for (size_t ai = 0; ai < n_arch; ++ai) {
        if (!mp_obj_is_str(arch_items[ai])) {
            continue;
        }
        const char *arch = mp_obj_str_get_str(arch_items[ai]);
        if (arch == NULL || arch[0] == '\0') {
            continue;
        }
        if (try_one_arch_ext(root, stem, allow_pkg, arch, aot_ext, url, path_out)) {
            return true;
        }
    }
    if (try_one_arch_ext(root, stem, allow_pkg, "", aot_ext, url, path_out)) {
        return true;
    }
    if (strcmp(aot_ext, ".aot") != 0) {
        for (size_t ai = 0; ai < n_arch; ++ai) {
            if (!mp_obj_is_str(arch_items[ai])) {
                continue;
            }
            const char *arch = mp_obj_str_get_str(arch_items[ai]);
            if (arch == NULL || arch[0] == '\0') {
                continue;
            }
            if (try_one_arch_ext(root, stem, allow_pkg, arch, ".aot", url, path_out)) {
                return true;
            }
        }
        if (try_one_arch_ext(root, stem, allow_pkg, "", ".aot", url, path_out)) {
            return true;
        }
    }
#else
    (void)root;
    (void)stem;
    (void)allow_pkg;
    (void)url;
    (void)path_out;
#endif
    return false;
}

static bool try_elf_variants(const char *root, const char *stem, bool allow_pkg,
    bool url, vstr_t *path_out) {
#if MICROPY_PY_WASM_ELF
    mp_wasm_arch_ensure();
    size_t n_arch = 0;
    mp_obj_t *arch_items = NULL;
    mp_obj_list_get(mp_wasm_arch_obj(), &n_arch, &arch_items);
    for (size_t ai = 0; ai < n_arch; ++ai) {
        if (!mp_obj_is_str(arch_items[ai])) {
            continue;
        }
        const char *arch = mp_obj_str_get_str(arch_items[ai]);
        if (arch == NULL || arch[0] == '\0') {
            continue;
        }
        if (try_one_arch_ext(root, stem, allow_pkg, arch, ".elf", url, path_out)) {
            return true;
        }
    }
    return try_one_arch_ext(root, stem, allow_pkg, "", ".elf", url, path_out);
#else
    (void)root;
    (void)stem;
    (void)allow_pkg;
    (void)url;
    (void)path_out;
    return false;
#endif
}

// Preference from MICROPY_WASM_CONTAINERS (compile-time string, e.g.
// "elf,aot,wasm"). Browser/Emscripten forces "wasm" only — no runtime switch.
static bool try_stem_variants(const char *root, const char *stem, bool allow_pkg,
    bool url, vstr_t *path_out) {
#ifndef MICROPY_WASM_CONTAINERS
#define MICROPY_WASM_CONTAINERS "elf,aot,wasm"
#endif
    const char *pref = MICROPY_WASM_CONTAINERS;
    const char *p = pref;
    while (*p) {
        while (*p == ',' || *p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        while (*p && *p != ',' && *p != ' ') {
            p++;
        }
        size_t n = (size_t)(p - start);
        if (n == 3 && memcmp(start, "elf", 3) == 0) {
            if (try_elf_variants(root, stem, allow_pkg, url, path_out)) {
                return true;
            }
        } else if (n == 3 && memcmp(start, "aot", 3) == 0) {
            if (try_aot_variants(root, stem, allow_pkg, url, path_out)) {
                return true;
            }
        } else if (n == 4 && memcmp(start, "wasm", 4) == 0) {
            if (try_one_arch_ext(root, stem, allow_pkg, "", ".wasm", url, path_out)) {
                return true;
            }
        }
    }
    // Fallback if preference string empty/unknown.
    if (try_elf_variants(root, stem, allow_pkg, url, path_out)) {
        return true;
    }
    if (try_aot_variants(root, stem, allow_pkg, url, path_out)) {
        return true;
    }
    return try_one_arch_ext(root, stem, allow_pkg, "", ".wasm", url, path_out);
}

static bool find_in_root(const char *root, const char *dotted_name, const char *slash_name,
    vstr_t *path_out) {
    if (root == NULL || path_is_frozen(root)) {
        return false;
    }
    if (root[0] == '\0') {
        root = ".";
    }
    bool url = mp_wasm_uri_is_http(root);

    // Metal CDN uses /artifacts/… only. Never treat the configured base as a
    // flat pack root — probing {base}/<name>.wasm can 200 HTML/JSON and fail verify.
    if (url && mp_wasm_cdn_driver() == MP_WASM_CDN_DRIVER_METAL
        && mp_wasm_cdn_url_is_base(root)) {
        return false;
    }

    // Path form: a/b/c → a/b/c/__init__.wasm | a/b/c.wasm (+ AOT variants)
    if (try_stem_variants(root, slash_name, true, url, path_out)) {
        return true;
    }

    // Flat dotted filename: a.b.c.wasm (handy in a single packs/ dir).
    if (strchr(dotted_name, '.') != NULL
        && try_stem_variants(root, dotted_name, false, url, path_out)) {
        return true;
    }

    return false;
}

static bool find_in_list(mp_obj_t list_obj, const char *dotted_name, const char *slash_name,
    vstr_t *path_out) {
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
        if (find_in_root(mp_obj_str_get_str(items[i]), dotted_name, slash_name, path_out)) {
            return true;
        }
    }
    return false;
}

bool mp_wasm_is_host_face(const char *dotted_name) {
    if (dotted_name == NULL || dotted_name[0] == '\0') {
        return false;
    }
    // Exact host faces + dotted children. Underscore after the stem
    // (wasmmod_examples) is intentionally not a match.
    static const char *const faces[] = {
        "pymergetic.wasmmod",
        "pymergetic.upy",
        "pymergetic.metal",
    };
    for (size_t i = 0; i < MP_ARRAY_SIZE(faces); ++i) {
        size_t n = strlen(faces[i]);
        if (strncmp(dotted_name, faces[i], n) == 0
            && (dotted_name[n] == '\0' || dotted_name[n] == '.')) {
            return true;
        }
    }
    return false;
}

bool mp_wasm_find_pack(const char *dotted_name, vstr_t *path_out) {
    if (dotted_name == NULL || dotted_name[0] == '\0' || mp_wasm_is_host_face(dotted_name)) {
        return false;
    }
    vstr_t slash;
    dotted_to_slash(dotted_name, &slash);
    const char *slash_name = vstr_null_terminated_str(&slash);

    mp_wasm_path_ensure();
    if (find_in_list(mp_wasm_path_obj(), dotted_name, slash_name, path_out)) {
        vstr_clear(&slash);
        return true;
    }
    #if MICROPY_PY_SYS_PATH
    if (find_in_list(mp_sys_path, dotted_name, slash_name, path_out)) {
        vstr_clear(&slash);
        return true;
    }
    #endif
    vstr_clear(&slash);
    return false;
}

// Import-hook prefer-path: wasm.path only (VFS + HTTP). Packs beat a
// same-named empty dir on cwd/sys.path; sys.path pack roots stay on ImportError.
bool mp_wasm_find_pack_on_wasm_path(const char *dotted_name, vstr_t *path_out) {
    if (dotted_name == NULL || dotted_name[0] == '\0' || mp_wasm_is_host_face(dotted_name)) {
        return false;
    }
    vstr_t slash;
    dotted_to_slash(dotted_name, &slash);
    mp_wasm_path_ensure();
    bool ok = find_in_list(mp_wasm_path_obj(), dotted_name, vstr_null_terminated_str(&slash), path_out);
    vstr_clear(&slash);
    return ok;
}

static mp_obj_t lookup_loaded(const char *name) {
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
    if (el == NULL || el->value == MP_OBJ_NULL) {
        return MP_OBJ_NULL;
    }
    return el->value;
}

static void link_on_parent(const char *dotted_name, mp_obj_t mod) {
    const char *dot = strrchr(dotted_name, '.');
    if (dot == NULL) {
        return;
    }
    size_t plen = (size_t)(dot - dotted_name);
    vstr_t parent;
    vstr_init(&parent, plen + 1);
    vstr_add_strn(&parent, dotted_name, plen);
    mp_obj_t pmod = lookup_loaded(vstr_null_terminated_str(&parent));
    vstr_clear(&parent);
    if (pmod != MP_OBJ_NULL) {
        mp_store_attr(pmod, qstr_from_strn(dot + 1, strlen(dot + 1)), mod);
    }
}

// PEP 420-style namespace package: no pack code, just __path__ + parent link.
static mp_obj_t ensure_namespace(const char *dotted_name) {
    mp_obj_t mod = lookup_loaded(dotted_name);
    if (mod != MP_OBJ_NULL) {
        return mod;
    }
    // Parents first so link_on_parent can attach us.
    const char *dot = strrchr(dotted_name, '.');
    if (dot != NULL) {
        size_t plen = (size_t)(dot - dotted_name);
        vstr_t parent;
        vstr_init(&parent, plen + 1);
        vstr_add_strn(&parent, dotted_name, plen);
        ensure_namespace(vstr_null_terminated_str(&parent));
        vstr_clear(&parent);
    }
    mod = lookup_loaded(dotted_name);
    if (mod != MP_OBJ_NULL) {
        return mod;
    }
    qstr q = qstr_from_str(dotted_name);
    mod = mp_obj_new_module(q);
    // MicroPython uses str __path__ (not a CPython-style list).
    mp_store_attr(mod, MP_QSTR___path__, mp_obj_new_str(dotted_name, strlen(dotted_name)));
    link_on_parent(dotted_name, mod);
    return mod;
}

// Map artifact filename → dotted pack stem. Strips optional .zlib, then
// .wasm / .elf / .aotN / .aot and a known arch infix. Returns false if not a pack artifact.
static bool artifact_to_stem(const char *fname, char *buf, size_t buf_len) {
    size_t n = strlen(fname);
    if (n > 5 && memcmp(fname + n - 5, ".zlib", 5) == 0) {
        n -= 5;
    }
    bool tagged = false;
    if (n > 5 && memcmp(fname + n - 5, ".wasm", 5) == 0) {
        n -= 5;
    } else if (n > 4 && memcmp(fname + n - 4, ".elf", 4) == 0) {
        n -= 4;
        tagged = true;
    } else {
        size_t alen = mp_wasm_aot_suffix_len_n(fname, n);
        if (alen == 0) {
            return false;
        }
        n -= alen;
        tagged = true;
    }
    if (n == 0 || n >= buf_len) {
        return false;
    }
    memcpy(buf, fname, n);
    buf[n] = '\0';

    if (tagged) {
        mp_wasm_arch_ensure();
        size_t n_arch = 0;
        mp_obj_t *arch_items = NULL;
        mp_obj_list_get(mp_wasm_arch_obj(), &n_arch, &arch_items);
        for (size_t ai = 0; ai < n_arch; ++ai) {
            if (!mp_obj_is_str(arch_items[ai])) {
                continue;
            }
            const char *arch = mp_obj_str_get_str(arch_items[ai]);
            size_t alen = arch ? strlen(arch) : 0;
            if (alen == 0 || n <= alen + 1) {
                continue;
            }
            if (buf[n - alen - 1] == '.' && memcmp(buf + n - alen, arch, alen) == 0) {
                n -= alen + 1;
                buf[n] = '\0';
                break;
            }
        }
    }
    return n > 0;
}

// Immediate child segment of stem under prefix (prefix="" → first segment).
// stem "a.b.c", prefix "a.b" → "c"; prefix "a" → "b"; prefix "" → "a".
static bool next_child_segment(const char *stem, const char *prefix,
    char *seg, size_t seg_len) {
    size_t plen = prefix ? strlen(prefix) : 0;
    const char *rest;
    if (plen == 0) {
        rest = stem;
    } else if (strncmp(stem, prefix, plen) == 0 && stem[plen] == '.') {
        rest = stem + plen + 1;
    } else {
        return false;
    }
    if (rest[0] == '\0') {
        return false; // exact match of prefix, not a child
    }
    const char *dot = strchr(rest, '.');
    size_t n = dot ? (size_t)(dot - rest) : strlen(rest);
    if (n == 0 || n >= seg_len) {
        return false;
    }
    memcpy(seg, rest, n);
    seg[n] = '\0';
    return true;
}

static bool stem_is_descendant(const char *stem, const char *prefix) {
    size_t plen = prefix ? strlen(prefix) : 0;
    if (plen == 0) {
        return stem[0] != '\0';
    }
    return strncmp(stem, prefix, plen) == 0 && stem[plen] == '.';
}

#if MICROPY_VFS
static bool listdir_names(const char *dirpath, mp_obj_t *list_out) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return false;
    }
    mp_obj_t args[1] = { mp_obj_new_str(dirpath, strlen(dirpath)) };
    mp_obj_t lst = mp_vfs_listdir(1, args);
    nlr_pop();
    if (!mp_obj_is_type(lst, &mp_type_list)) {
        return false;
    }
    *list_out = lst;
    return true;
}

// Append unique child segment strings to out_list.
static void note_child(mp_obj_t out_list, const char *seg) {
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(out_list, &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (mp_obj_is_str(items[i]) && strcmp(mp_obj_str_get_str(items[i]), seg) == 0) {
            return;
        }
    }
    mp_obj_list_append(out_list, mp_obj_new_str(seg, strlen(seg)));
}

static void scan_root_children(const char *root, const char *prefix, mp_obj_t out_list) {
    if (root == NULL || path_is_frozen(root) || mp_wasm_uri_is_http(root)) {
        return;
    }
    if (root[0] == '\0') {
        root = ".";
    }

    // Flat artifacts in the pack root: a.b.c.wasm / a.b.c.<arch>.aot
    mp_obj_t names = MP_OBJ_NULL;
    if (listdir_names(root, &names)) {
        size_t n;
        mp_obj_t *items;
        mp_obj_list_get(names, &n, &items);
        char stem[MP_WASM_NAME_MAX + 1];
        char seg[64];
        for (size_t i = 0; i < n; ++i) {
            if (!mp_obj_is_str(items[i])) {
                continue;
            }
            if (!artifact_to_stem(mp_obj_str_get_str(items[i]), stem, sizeof(stem))) {
                continue;
            }
            if (!stem_is_descendant(stem, prefix)) {
                continue;
            }
            if (next_child_segment(stem, prefix, seg, sizeof(seg))) {
                note_child(out_list, seg);
            }
        }
    }

    // Tree form: root[/prefix_as_slashes]/ — dirs and pack files as children.
    vstr_t dir;
    if (prefix == NULL || prefix[0] == '\0') {
        vstr_init(&dir, strlen(root) + 1);
        vstr_add_str(&dir, root);
    } else {
        vstr_t slash;
        dotted_to_slash(prefix, &slash);
        mp_wasm_join_uri(root, vstr_null_terminated_str(&slash), &dir);
        vstr_clear(&slash);
    }
    const char *dirpath = vstr_null_terminated_str(&dir);
    if (mp_import_stat(dirpath) == MP_IMPORT_STAT_DIR) {
        mp_obj_t tnames = MP_OBJ_NULL;
        if (listdir_names(dirpath, &tnames)) {
            size_t n;
            mp_obj_t *items;
            mp_obj_list_get(tnames, &n, &items);
            char stem[MP_WASM_NAME_MAX + 1];
            for (size_t i = 0; i < n; ++i) {
                if (!mp_obj_is_str(items[i])) {
                    continue;
                }
                const char *fn = mp_obj_str_get_str(items[i]);
                if (strcmp(fn, ".") == 0 || strcmp(fn, "..") == 0) {
                    continue;
                }
                // Directory child → namespace segment.
                vstr_t child_path;
                mp_wasm_join_uri(dirpath, fn, &child_path);
                mp_import_stat_t st = mp_import_stat(vstr_null_terminated_str(&child_path));
                vstr_clear(&child_path);
                if (st == MP_IMPORT_STAT_DIR) {
                    note_child(out_list, fn);
                    continue;
                }
                // File child: foo.wasm → segment "foo" (ignore __init__*).
                if (artifact_to_stem(fn, stem, sizeof(stem))) {
                    if (strcmp(stem, "__init__") != 0) {
                        // stem may still be dotted if weird; take first segment only
                        char *dot = strchr(stem, '.');
                        if (dot) {
                            *dot = '\0';
                        }
                        if (stem[0] != '\0') {
                            note_child(out_list, stem);
                        }
                    }
                }
            }
        }
    }
    vstr_clear(&dir);
}

static void collect_children(const char *prefix, mp_obj_t out_list) {
    mp_wasm_path_ensure();
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(mp_wasm_path_obj(), &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (mp_obj_is_str(items[i])) {
            scan_root_children(mp_obj_str_get_str(items[i]), prefix, out_list);
        }
    }
    #if MICROPY_PY_SYS_PATH
    mp_obj_list_get(mp_sys_path, &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (mp_obj_is_str(items[i])) {
            scan_root_children(mp_obj_str_get_str(items[i]), prefix, out_list);
        }
    }
    #endif
}
#endif // MICROPY_VFS

bool mp_wasm_has_descendants(const char *prefix) {
    if (prefix == NULL || prefix[0] == '\0') {
        return false;
    }
    // Metal CDN has no local directory listing; allow intermediate namespaces so
    // `from test_a.test_b import test_c` can resolve the leaf via CDN fetch.
    if (mp_wasm_cdn_driver() == MP_WASM_CDN_DRIVER_METAL) {
        return true;
    }
    #if MICROPY_VFS
    mp_obj_t kids = mp_obj_new_list(0, NULL);
    collect_children(prefix, kids);
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(kids, &n, &items);
    return n > 0;
    #else
    (void)prefix;
    return false;
    #endif
}

bool mp_wasm_query_import(const char *dotted_name) {
    if (mp_wasm_is_host_face(dotted_name)) {
        return false;
    }
    vstr_t path;
    if (mp_wasm_find_pack(dotted_name, &path)) {
        vstr_clear(&path);
        return true;
    }
    return mp_wasm_has_descendants(dotted_name);
}

// Create namespace shells for intermediate segments implied by pack files.
static void discover_fill(const char *prefix) {
    #if MICROPY_VFS
    mp_obj_t kids = mp_obj_new_list(0, NULL);
    collect_children(prefix, kids);
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(kids, &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (!mp_obj_is_str(items[i])) {
            continue;
        }
        const char *seg = mp_obj_str_get_str(items[i]);
        vstr_t full;
        if (prefix[0] == '\0') {
            vstr_init(&full, strlen(seg) + 1);
            vstr_add_str(&full, seg);
        } else {
            vstr_init(&full, strlen(prefix) + strlen(seg) + 2);
            vstr_add_str(&full, prefix);
            vstr_add_char(&full, '.');
            vstr_add_str(&full, seg);
        }
        const char *child = vstr_null_terminated_str(&full);
        // Own pack → real module; load on demand. Still recurse? no.
        vstr_t path;
        if (mp_wasm_find_pack(child, &path)) {
            vstr_clear(&path);
            vstr_clear(&full);
            continue;
        }
        if (mp_wasm_has_descendants(child)) {
            ensure_namespace(child);
            discover_fill(child);
        }
        vstr_clear(&full);
    }
    #else
    (void)prefix;
    #endif
}

mp_obj_t mp_wasm_import_wasm(const char *dotted_name) {
    return mp_wasm_import_wasm_at(dotted_name, NULL);
}

mp_obj_t mp_wasm_import_wasm_at(const char *dotted_name, const char *known_path) {
    mp_obj_t existing = lookup_loaded(dotted_name);
    if (existing != MP_OBJ_NULL) {
        return existing;
    }

    // Host/kernel faces are builtin modules — never fetch/instantiate the engine.
    if (mp_wasm_is_host_face(dotted_name)) {
        mp_raise_msg_varg(&mp_type_ImportError,
            MP_ERROR_TEXT("host face '%s' is not a guest pack"), dotted_name);
    }

    // Metal CDN first: /artifacts/lead|pin (prefer .zlib). Skip flat wasm.path
    // probes against the configured base (non-pack bodies that break verify).
    if (known_path == NULL && mp_wasm_cdn_driver() == MP_WASM_CDN_DRIVER_METAL) {
        uint8_t *bytes = NULL;
        uint32_t blen = 0;
        char err[160];
        char origin[MP_WASM_ORIGIN_MAX];
        if (mp_wasm_cdn_fetch_pack_ex(dotted_name, NULL, &bytes, &blen,
                origin, sizeof(origin), err, sizeof(err))) {
            const char *dot = strrchr(dotted_name, '.');
            if (dot != NULL) {
                size_t plen = (size_t)(dot - dotted_name);
                vstr_t parent;
                vstr_init(&parent, plen + 1);
                vstr_add_strn(&parent, dotted_name, plen);
                const char *pname = vstr_null_terminated_str(&parent);
                if (lookup_loaded(pname) == MP_OBJ_NULL) {
                    nlr_buf_t nlr;
                    if (nlr_push(&nlr) == 0) {
                        (void)mp_wasm_import_wasm(pname);
                        nlr_pop();
                    } else {
                        ensure_namespace(pname);
                    }
                }
                vstr_clear(&parent);
            }
            mp_obj_t mod = mp_pack_load_bytes_at(bytes, blen, dotted_name,
                origin[0] != '\0' ? origin : NULL);
            MICROPY_WASM_FREE(bytes);
            existing = lookup_loaded(dotted_name);
            if (existing != MP_OBJ_NULL) {
                mod = existing;
            }
            link_on_parent(dotted_name, mod);
            return mod;
        }
        // Metal miss: do not probe flat wasm.path (CDN base ≠ pack mirror).
        // Dotted intermediates → namespace; else try embedded submodule / ImportError.
        if (strchr(dotted_name, '.') != NULL && mp_wasm_has_descendants(dotted_name)) {
            return ensure_namespace(dotted_name);
        }
        const char *dot = strrchr(dotted_name, '.');
        if (dot != NULL) {
            size_t plen = (size_t)(dot - dotted_name);
            vstr_t parent;
            vstr_init(&parent, plen + 1);
            vstr_add_strn(&parent, dotted_name, plen);
            const char *parent_name = vstr_null_terminated_str(&parent);
            if (lookup_loaded(parent_name) == MP_OBJ_NULL) {
                nlr_buf_t nlr;
                if (nlr_push(&nlr) == 0) {
                    (void)mp_wasm_import_wasm(parent_name);
                    nlr_pop();
                } else {
                    ensure_namespace(parent_name);
                }
            }
            vstr_clear(&parent);
            existing = lookup_loaded(dotted_name);
            if (existing != MP_OBJ_NULL) {
                return existing;
            }
        }
        mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("no wasm pack named '%s'"), dotted_name);
    }

    // Leaf pack at any depth — parents become namespaces if needed.
    vstr_t path;
    bool have_path = false;
    if (known_path != NULL && known_path[0] != '\0') {
        vstr_init(&path, strlen(known_path) + 1);
        vstr_add_str(&path, known_path);
        have_path = true;
    } else if (mp_wasm_find_pack(dotted_name, &path)) {
        have_path = true;
    }

    if (have_path) {
        const char *dot = strrchr(dotted_name, '.');
        if (dot != NULL) {
            size_t plen = (size_t)(dot - dotted_name);
            vstr_t parent;
            vstr_init(&parent, plen + 1);
            vstr_add_strn(&parent, dotted_name, plen);
            const char *pname = vstr_null_terminated_str(&parent);
            // Prefer a real parent pack; else namespace + discover children.
            if (lookup_loaded(pname) == MP_OBJ_NULL) {
                vstr_t ppath;
                if (mp_wasm_find_pack(pname, &ppath)) {
                    vstr_clear(&ppath);
                    (void)mp_wasm_import_wasm(pname);
                } else if (mp_wasm_cdn_driver() == MP_WASM_CDN_DRIVER_METAL) {
                    (void)mp_wasm_import_wasm(pname);
                } else {
                    ensure_namespace(pname);
                    discover_fill(pname);
                }
            }
            vstr_clear(&parent);
        }
        mp_obj_t mod = mp_pack_load_path(vstr_null_terminated_str(&path), dotted_name);
        vstr_clear(&path);
        existing = lookup_loaded(dotted_name);
        if (existing != MP_OBJ_NULL) {
            mod = existing;
        }
        link_on_parent(dotted_name, mod);
        return mod;
    }

    // No leaf: namespace package if pack files live underneath (PEP 420-ish).
    if (mp_wasm_has_descendants(dotted_name)) {
        mp_obj_t ns = ensure_namespace(dotted_name);
        discover_fill(dotted_name);
        return ns;
    }

    // Embedded submodule of a parent pack (hello.util).
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

    mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("no wasm pack named '%s'"), dotted_name);
}

#endif // MICROPY_PY_WASM
