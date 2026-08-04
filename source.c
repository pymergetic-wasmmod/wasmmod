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

#include "py/mpstate.h"
#include "py/nlr.h"
#include "py/objmodule.h"
#include "py/reader.h"

#include "extmod/wasmmod/alloc.h"
#include "extmod/wasmmod/mod.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/source.h"
#include "extmod/wasmmod/zlibutil.h"

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct mp_wasm_source_view_t {
    mp_wasm_source_info_t info;
    uint8_t *owned; // malloc'd wasm bytes when opened from file; else NULL
    uint32_t owned_len;
};

bool mp_wasm_source_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len) {
    return mp_wasm_find_custom_section(wasm, len, MP_WASM_SOURCE_SECTION, payload, payload_len);
}

bool mp_wasm_source_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_source_info_t *out) {
    memset(out, 0, sizeof(*out));
    if (payload_len < 12 || memcmp(payload, MP_WASM_SOURCE_MAGIC, 4) != 0) {
        return false;
    }
    out->version = read_u16_le(payload + 4);
    out->flags = read_u16_le(payload + 6);
    if (out->version != MP_WASM_SOURCE_VERSION) {
        return false;
    }
    uint16_t name_len = read_u16_le(payload + 8);
    if ((uint32_t)10 + name_len + 2 > payload_len) {
        return false;
    }
    out->name = (const char *)(payload + 10);
    out->name_len = name_len;
    const uint8_t *p = payload + 10 + name_len;
    uint16_t ver_len = read_u16_le(p);
    p += 2;
    if ((uint32_t)(p - payload) + ver_len + 2 > payload_len) {
        return false;
    }
    out->pkg_version = (const char *)p;
    out->pkg_version_len = ver_len;
    p += ver_len;
    uint16_t n_tags = read_u16_le(p);
    p += 2;
    if (n_tags > 256) {
        return false;
    }
    mp_wasm_source_tag_t *tags = NULL;
    if (n_tags > 0) {
        tags = MICROPY_WASM_MALLOC(n_tags * sizeof(mp_wasm_source_tag_t));
        if (tags == NULL) {
            return false;
        }
    }
    for (uint16_t i = 0; i < n_tags; ++i) {
        if ((uint32_t)(p - payload) + 2 > payload_len) {
            goto fail;
        }
        uint16_t kl = read_u16_le(p);
        p += 2;
        if ((uint32_t)(p - payload) + kl + 2 > payload_len) {
            goto fail;
        }
        tags[i].key = (const char *)p;
        tags[i].key_len = kl;
        p += kl;
        uint16_t vl = read_u16_le(p);
        p += 2;
        if ((uint32_t)(p - payload) + vl > payload_len) {
            goto fail;
        }
        tags[i].value = (const char *)p;
        tags[i].value_len = vl;
        p += vl;
    }
    if ((uint32_t)(p - payload) + 4 > payload_len) {
        goto fail;
    }
    uint32_t n_files = read_u32_le(p);
    p += 4;
    if (n_files > 4096) {
        goto fail;
    }
    mp_wasm_source_file_t *files = NULL;
    if (n_files > 0) {
        files = MICROPY_WASM_MALLOC(n_files * sizeof(mp_wasm_source_file_t));
        if (files == NULL) {
            goto fail;
        }
    }
    for (uint32_t i = 0; i < n_files; ++i) {
        if ((uint32_t)(p - payload) + 2 > payload_len) {
            MICROPY_WASM_FREE(files);
            goto fail;
        }
        uint16_t path_len = read_u16_le(p);
        p += 2;
        if ((uint32_t)(p - payload) + path_len + 1 + 4 + 4 > payload_len) {
            MICROPY_WASM_FREE(files);
            goto fail;
        }
        files[i].path = (const char *)p;
        files[i].path_len = path_len;
        p += path_len;
        files[i].flags = *p++;
        files[i].raw_len = read_u32_le(p);
        p += 4;
        uint32_t data_len = read_u32_le(p);
        p += 4;
        if ((uint32_t)(p - payload) + data_len > payload_len) {
            MICROPY_WASM_FREE(files);
            goto fail;
        }
        files[i].data = p;
        files[i].data_len = data_len;
        p += data_len;
    }
    out->tags = tags;
    out->n_tags = n_tags;
    out->files = files;
    out->n_files = n_files;
    return true;

fail:
    MICROPY_WASM_FREE(tags);
    memset(out, 0, sizeof(*out));
    return false;
}

void mp_wasm_source_info_free(mp_wasm_source_info_t *info) {
    if (info == NULL) {
        return;
    }
    MICROPY_WASM_FREE((void *)info->tags);
    MICROPY_WASM_FREE((void *)info->files);
    info->tags = NULL;
    info->files = NULL;
    info->n_tags = 0;
    info->n_files = 0;
}

static mp_wasm_source_view_t *view_from_wasm(const uint8_t *wasm, uint32_t len, uint8_t *owned) {
    const uint8_t *payload;
    uint32_t payload_len;
    if (!mp_wasm_source_find_section(wasm, len, &payload, &payload_len)) {
        MICROPY_WASM_FREE(owned);
        return NULL;
    }
    mp_wasm_source_view_t *v = MICROPY_WASM_MALLOC(sizeof(*v));
    if (v == NULL) {
        MICROPY_WASM_FREE(owned);
        return NULL;
    }
    memset(v, 0, sizeof(*v));
    if (!mp_wasm_source_parse(payload, payload_len, &v->info)) {
        MICROPY_WASM_FREE(v);
        MICROPY_WASM_FREE(owned);
        return NULL;
    }
    v->owned = owned;
    v->owned_len = owned ? len : 0;
    return v;
}

mp_wasm_source_view_t *mp_wasm_source_open_buffer(const uint8_t *wasm, uint32_t len) {
    if (wasm == NULL || len == 0) {
        return NULL;
    }
    return view_from_wasm(wasm, len, NULL);
}

mp_wasm_source_view_t *mp_wasm_source_open_owned(uint8_t *wasm, uint32_t len) {
    if (wasm == NULL || len == 0) {
        MICROPY_WASM_FREE(wasm);
        return NULL;
    }
    return view_from_wasm(wasm, len, wasm);
}

mp_wasm_source_view_t *mp_wasm_source_open_file(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return NULL;
    }
    mp_reader_t reader;
    mp_reader_new_file(&reader, qstr_from_str(path));
    vstr_t vs;
    vstr_init(&vs, 256);
    for (;;) {
        mp_uint_t b = reader.readbyte(reader.data);
        if (b == MP_READER_EOF) {
            break;
        }
        vstr_add_byte(&vs, (byte)b);
        if (vs.len > 64u * 1024u * 1024u) {
            reader.close(reader.data);
            vstr_clear(&vs);
            nlr_pop();
            return NULL;
        }
    }
    reader.close(reader.data);
    nlr_pop();
    uint32_t len = (uint32_t)vs.len;
    uint8_t *buf = MICROPY_WASM_MALLOC(len ? len : 1);
    if (buf == NULL) {
        vstr_clear(&vs);
        return NULL;
    }
    if (len) {
        memcpy(buf, vs.buf, len);
    }
    vstr_clear(&vs);
    return view_from_wasm(buf, len, buf);
}

mp_wasm_source_view_t *mp_wasm_source_open_name(const char *pack_name) {
    if (pack_name == NULL || pack_name[0] == '\0') {
        return NULL;
    }
    qstr q = qstr_from_str(pack_name);
    mp_map_elem_t *elem = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map, MP_OBJ_NEW_QSTR(q), MP_MAP_LOOKUP);
    if (elem == NULL) {
        return NULL;
    }
    mp_obj_t mod = elem->value;
    if (!mp_obj_is_type(mod, &mp_type_module)) {
        return NULL;
    }
    mp_map_elem_t *we = mp_map_lookup(&mp_obj_module_get_globals(mod)->map,
        MP_OBJ_NEW_QSTR(MP_QSTR___pack__), MP_MAP_LOOKUP);
    if (we == NULL || !mp_obj_is_type(we->value, &mp_type_pack_module)) {
        return NULL;
    }
    mp_obj_pack_module_t *wo = MP_OBJ_TO_PTR(we->value);
    if (wo->mod == NULL) {
        return NULL;
    }
    uint32_t len = 0;
    const uint8_t *bytes = mp_pack_meta_bytes(wo->mod, &len);
    if (bytes == NULL || len == 0) {
        bytes = mp_pack_bytes(wo->mod, &len);
    }
    if (bytes == NULL || len == 0) {
        return NULL;
    }
    return mp_wasm_source_open_buffer(bytes, len);
}

void mp_wasm_source_close(mp_wasm_source_view_t *v) {
    if (v == NULL) {
        return;
    }
    mp_wasm_source_info_free(&v->info);
    MICROPY_WASM_FREE(v->owned);
    MICROPY_WASM_FREE(v);
}

const mp_wasm_source_info_t *mp_wasm_source_info(const mp_wasm_source_view_t *v) {
    return v == NULL ? NULL : &v->info;
}

bool mp_wasm_source_read(const mp_wasm_source_view_t *v, const char *path, uint8_t **out, uint32_t *out_len) {
    if (v == NULL || path == NULL || out == NULL || out_len == NULL) {
        return false;
    }
    size_t want = strlen(path);
    for (uint32_t i = 0; i < v->info.n_files; ++i) {
        const mp_wasm_source_file_t *f = &v->info.files[i];
        if (f->path_len != want || memcmp(f->path, path, want) != 0) {
            continue;
        }
        if ((f->flags & MP_WASM_SOURCE_FILE_FLAG_ZLIB) == 0) {
            uint8_t *copy = MICROPY_WASM_MALLOC(f->data_len ? f->data_len : 1);
            if (copy == NULL) {
                return false;
            }
            if (f->data_len) {
                memcpy(copy, f->data, f->data_len);
            }
            *out = copy;
            *out_len = f->data_len;
            return true;
        }
        uint8_t *raw = MICROPY_WASM_MALLOC(f->raw_len ? f->raw_len : 1);
        if (raw == NULL) {
            return false;
        }
        if (!mp_wasm_zlib_inflate(f->data, f->data_len, raw, f->raw_len)) {
            MICROPY_WASM_FREE(raw);
            return false;
        }
        *out = raw;
        *out_len = f->raw_len;
        return true;
    }
    return false;
}

size_t mp_wasm_source_mount_prefix(const mp_wasm_source_view_t *v, char *buf, size_t buf_len) {
    if (v == NULL || buf == NULL || buf_len < 5) {
        return 0;
    }
    // Prefer "src/" when any path starts with it (matches default pack layout).
    for (uint32_t i = 0; i < v->info.n_files; ++i) {
        const mp_wasm_source_file_t *f = &v->info.files[i];
        if (f->path_len >= 4 && memcmp(f->path, "src/", 4) == 0) {
            memcpy(buf, "src/", 4);
            buf[4] = '\0';
            return 4;
        }
    }
    buf[0] = '\0';
    return 0;
}

static bool path_is_under_module(const char *path, size_t path_len,
    const char *mount, size_t mount_len, const char *mod, size_t mod_len,
    bool *is_direct_file) {
    // path like mount + mod.replace('.','/') + '/' + rest  OR  mount + mod + '.py'
    size_t need = mount_len + (mod_len ? mod_len + 1 : 0);
    if (path_len < mount_len || memcmp(path, mount, mount_len) != 0) {
        return false;
    }
    const char *rest = path + mount_len;
    size_t rest_len = path_len - mount_len;
    if (mod_len == 0) {
        // root module: files directly under mount (no '/' before last segment for packages —
        // include all under mount for pack-root module listing of "this package's files")
        *is_direct_file = true;
        return true;
    }
    // Compare dotted mod against path segments.
    size_t pi = 0, mi = 0;
    while (mi < mod_len) {
        if (pi >= rest_len) {
            return false;
        }
        if (mod[mi] == '.') {
            if (rest[pi] != '/') {
                return false;
            }
            pi++;
            mi++;
            continue;
        }
        if (rest[pi] != mod[mi]) {
            return false;
        }
        pi++;
        mi++;
    }
    if (pi == rest_len) {
        return false; // exact dir name without file — not a file path
    }
    if (rest[pi] == '/') {
        *is_direct_file = true;
        return true;
    }
    if (rest[pi] == '.' && mod_len > 0) {
        // e.g. util.py for module util — treat as module file
        *is_direct_file = true;
        return true;
    }
    (void)need;
    return false;
}

int mp_wasm_source_list_files(const mp_wasm_source_view_t *v, const char *module_or_null,
    void *ctx, mp_wasm_source_path_cb cb) {
    if (v == NULL || cb == NULL) {
        return -1;
    }
    if (module_or_null == NULL) {
        for (uint32_t i = 0; i < v->info.n_files; ++i) {
            const mp_wasm_source_file_t *f = &v->info.files[i];
            int r = cb(ctx, f->path, f->path_len);
            if (r != 0) {
                return r;
            }
        }
        return 0;
    }
    char mount[16];
    size_t mount_len = mp_wasm_source_mount_prefix(v, mount, sizeof(mount));
    size_t mod_len = strlen(module_or_null);
    for (uint32_t i = 0; i < v->info.n_files; ++i) {
        const mp_wasm_source_file_t *f = &v->info.files[i];
        bool direct = false;
        if (!path_is_under_module(f->path, f->path_len, mount, mount_len, module_or_null, mod_len, &direct)) {
            continue;
        }
        // Restrict to this module's directory (not nested package file bodies beyond first child).
        // Include all under prefix for simplicity; submodule pointers are separate.
        const char *rest = f->path + mount_len;
        size_t rest_len = f->path_len - mount_len;
        // After matching module path, remaining should not contain another package __init__ depth
        // for "files only at this level": allow files in this dir and non-package files.
        if (mod_len > 0) {
            // skip past "util/" or "util."
            size_t pi = 0, mi = 0;
            while (mi < mod_len && pi < rest_len) {
                if (module_or_null[mi] == '.') {
                    if (rest[pi] != '/') {
                        break;
                    }
                    pi++;
                    mi++;
                } else if (rest[pi] == module_or_null[mi]) {
                    pi++;
                    mi++;
                } else {
                    break;
                }
            }
            if (mi != mod_len) {
                continue;
            }
            if (pi < rest_len && rest[pi] == '/') {
                pi++;
                // reject nested package bodies: if rest has '/' and a child __init__, still list
                // everything under for now (module view includes subtree files except we filter
                // via submodules for pointers). Plan: files = this dir + co-located; not children.
                const char *slash = memchr(rest + pi, '/', rest_len - pi);
                if (slash != NULL) {
                    continue; // nested path → belongs to child module
                }
            } else if (pi < rest_len && rest[pi] == '.') {
                // util.py style
            } else {
                continue;
            }
        } else {
            // root: only files directly under mount (no '/')
            if (memchr(rest, '/', rest_len) != NULL) {
                continue;
            }
        }
        (void)direct;
        int r = cb(ctx, f->path, f->path_len);
        if (r != 0) {
            return r;
        }
    }
    return 0;
}

static void dotted_from_py_path(const char *rel, size_t rel_len, char *out, size_t out_len, size_t *out_n) {
    // rel is under mount, e.g. "util/__init__.py" or "util/extra.py" or "__init__.py"
    size_t n = rel_len;
    if (n >= 12 && memcmp(rel + n - 12, "/__init__.py", 12) == 0) {
        n -= 12;
    } else if (n == 11 && memcmp(rel, "__init__.py", 11) == 0) {
        n = 0;
    } else if (n >= 3 && memcmp(rel + n - 3, ".py", 3) == 0) {
        n -= 3;
    } else {
        *out_n = 0;
        return; // not a python module path
    }
    if (n + 1 > out_len) {
        *out_n = 0;
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = (rel[i] == '/') ? '.' : rel[i];
    }
    out[n] = '\0';
    *out_n = n;
}

int mp_wasm_source_list_modules(const mp_wasm_source_view_t *v, void *ctx, mp_wasm_source_name_cb cb) {
    if (v == NULL || cb == NULL) {
        return -1;
    }
    char mount[16];
    size_t mount_len = mp_wasm_source_mount_prefix(v, mount, sizeof(mount));
    // Collect unique dotted names (small n_files; O(n^2) ok).
    char names[64][128];
    size_t n_names = 0;
    for (uint32_t i = 0; i < v->info.n_files; ++i) {
        const mp_wasm_source_file_t *f = &v->info.files[i];
        if (f->path_len < mount_len || memcmp(f->path, mount, mount_len) != 0) {
            continue;
        }
        if (f->path_len < 3 || memcmp(f->path + f->path_len - 3, ".py", 3) != 0) {
            continue;
        }
        char dotted[128];
        size_t dn = 0;
        dotted_from_py_path(f->path + mount_len, f->path_len - mount_len, dotted, sizeof(dotted), &dn);
        if (dn == 0 && !(f->path_len - mount_len == 11 && memcmp(f->path + mount_len, "__init__.py", 11) == 0)) {
            // skip non-module; root __init__ → empty name, still a module (pack root)
            if (!(f->path_len - mount_len == 11 && memcmp(f->path + mount_len, "__init__.py", 11) == 0)) {
                continue;
            }
            dn = 0;
            dotted[0] = '\0';
        }
        bool seen = false;
        for (size_t j = 0; j < n_names; ++j) {
            if (strcmp(names[j], dotted) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen && n_names < 64) {
            strncpy(names[n_names], dotted, sizeof(names[0]) - 1);
            names[n_names][sizeof(names[0]) - 1] = '\0';
            n_names++;
        }
    }
    for (size_t j = 0; j < n_names; ++j) {
        int r = cb(ctx, names[j], strlen(names[j]));
        if (r != 0) {
            return r;
        }
    }
    return 0;
}

int mp_wasm_source_list_submodules(const mp_wasm_source_view_t *v, const char *parent,
    void *ctx, mp_wasm_source_name_cb cb) {
    if (v == NULL || cb == NULL) {
        return -1;
    }
    if (parent == NULL) {
        parent = "";
    }
    char mount[16];
    size_t mount_len = mp_wasm_source_mount_prefix(v, mount, sizeof(mount));
    size_t parent_len = strlen(parent);
    char children[32][64];
    size_t n_ch = 0;
    for (uint32_t i = 0; i < v->info.n_files; ++i) {
        const mp_wasm_source_file_t *f = &v->info.files[i];
        if (f->path_len < mount_len || memcmp(f->path, mount, mount_len) != 0) {
            continue;
        }
        if (f->path_len < 3 || memcmp(f->path + f->path_len - 3, ".py", 3) != 0) {
            continue;
        }
        char dotted[128];
        size_t dn = 0;
        dotted_from_py_path(f->path + mount_len, f->path_len - mount_len, dotted, sizeof(dotted), &dn);
        if (dn == 0) {
            continue;
        }
        if (parent_len == 0) {
            // top-level: first segment only
            const char *dot = strchr(dotted, '.');
            size_t seg = dot ? (size_t)(dot - dotted) : dn;
            char child[64];
            if (seg >= sizeof(child)) {
                continue;
            }
            memcpy(child, dotted, seg);
            child[seg] = '\0';
            bool seen = false;
            for (size_t j = 0; j < n_ch; ++j) {
                if (strcmp(children[j], child) == 0) {
                    seen = true;
                    break;
                }
            }
            if (!seen && n_ch < 32) {
                strcpy(children[n_ch++], child);
            }
        } else if (dn > parent_len + 1 && memcmp(dotted, parent, parent_len) == 0 && dotted[parent_len] == '.') {
            const char *rest = dotted + parent_len + 1;
            const char *dot = strchr(rest, '.');
            size_t seg = dot ? (size_t)(dot - rest) : strlen(rest);
            char child[64];
            if (seg >= sizeof(child)) {
                continue;
            }
            memcpy(child, rest, seg);
            child[seg] = '\0';
            bool seen = false;
            for (size_t j = 0; j < n_ch; ++j) {
                if (strcmp(children[j], child) == 0) {
                    seen = true;
                    break;
                }
            }
            if (!seen && n_ch < 32) {
                strcpy(children[n_ch++], child);
            }
        }
    }
    for (size_t j = 0; j < n_ch; ++j) {
        int r = cb(ctx, children[j], strlen(children[j]));
        if (r != 0) {
            return r;
        }
    }
    return 0;
}

#endif // MICROPY_PY_WASM
