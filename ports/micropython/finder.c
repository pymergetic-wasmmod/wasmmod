/*
 * Pack path finder — see finder.h.
 * Containers: .elf / .aotN / .aot / .wasm, each optionally + .zlib.
 * HTTP roots: io.probe / io.fetch. Metal-cdn bases on wasm.path are skipped
 * (artifacts/lead|pin via net.cdn). Local: µPy VFS.
 */

#include "ports/micropython/finder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/builtin.h"
#include "py/mperrno.h"
#include "py/nlr.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/stream.h"
#if MICROPY_VFS
#include "extmod/vfs.h"
#endif

#if !MICROPY_PY_SYS_PATH
#error "wasmmod pack finder requires MICROPY_PY_SYS_PATH"
#endif

#include "ports/micropython/packbind.h"
#include "pymergetic/util/version/__exports__.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/net/cdn.h"
#include "pymergetic/wasmmod/pack/__types__.h"
#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/format/common/format.h"
#include "pymergetic/wasmmod/pack/zlib_env.h"
#include "pymergetic/wasmmod/registry/__exports__.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#ifndef MICROPY_WASM_AOT_VERSION
#define MICROPY_WASM_AOT_VERSION (0)
#endif

#ifndef MICROPY_WASM_CONTAINERS
#define MICROPY_WASM_CONTAINERS "elf,aot,wasm"
#endif

#ifndef MICROPY_WASM_VERIFY
#define MICROPY_WASM_VERIFY (0)
#endif

#if MICROPY_WASM_VERIFY
#include "pymergetic/wasmmod/verify/__exports__.h"
#endif

/* Root pointer name must not match the getter (clangd/type confusion). */
MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_path);

#define MP_WASM_LOCAL_PACKS 4

typedef struct {
    const char *name;
    const uint8_t *bytes;
    size_t len;
} mp_wasm_local_pack_t;

static mp_wasm_local_pack_t s_local_packs[MP_WASM_LOCAL_PACKS];

void mp_wasm_register_local_bytes(const char *dotted_name, const uint8_t *bytes, size_t len) {
    size_t i;
    if (dotted_name == NULL || dotted_name[0] == '\0' || bytes == NULL || len == 0) {
        return;
    }
    for (i = 0; i < MP_WASM_LOCAL_PACKS; ++i) {
        if (s_local_packs[i].name != NULL && strcmp(s_local_packs[i].name, dotted_name) == 0) {
            s_local_packs[i].bytes = bytes;
            s_local_packs[i].len = len;
            return;
        }
    }
    for (i = 0; i < MP_WASM_LOCAL_PACKS; ++i) {
        if (s_local_packs[i].name == NULL) {
            s_local_packs[i].name = dotted_name;
            s_local_packs[i].bytes = bytes;
            s_local_packs[i].len = len;
            return;
        }
    }
}

static const mp_wasm_local_pack_t *local_pack_by_name(const char *dotted_name) {
    size_t i;
    for (i = 0; i < MP_WASM_LOCAL_PACKS; ++i) {
        if (s_local_packs[i].name != NULL && strcmp(s_local_packs[i].name, dotted_name) == 0) {
            return &s_local_packs[i];
        }
    }
    return NULL;
}

static int bytes_is_local_pack(const uint8_t *bytes) {
    size_t i;
    if (bytes == NULL) {
        return 0;
    }
    for (i = 0; i < MP_WASM_LOCAL_PACKS; ++i) {
        if (s_local_packs[i].bytes == bytes) {
            return 1;
        }
    }
    return 0;
}

static void pack_bytes_free(uint8_t *bytes, size_t len) {
    if (!bytes_is_local_pack(bytes)) {
        m_del(uint8_t, bytes, len);
    }
}

/* io.fetch / net.cdn bytes: cargo TUs malloc via alloc.h stdlib. Unix
 * MICROPY_PY_METAL=1 redirects this TU's MICROPY_WASM_FREE to TLSF —
 * do not mix. Firmware/emcc compile io in the same image. */
static void io_fetch_free(uint8_t *buf) {
#if defined(__EMSCRIPTEN__) || defined(PM_METAL_FIRMWARE)
    MICROPY_WASM_FREE(buf);
#else
    free(buf);
#endif
}

static bool local_pack_path(const char *dotted_name, vstr_t *path_out) {
    if (local_pack_by_name(dotted_name) == NULL) {
        return false;
    }
    vstr_init(path_out, 4 + strlen(dotted_name) + 1);
    vstr_add_str(path_out, "mem:");
    vstr_add_str(path_out, dotted_name);
    return true;
}

bool mp_wasm_is_host_face(const char *dotted_name) {
    if (dotted_name == NULL || dotted_name[0] == '\0') {
        return false;
    }
    /* Bare umbrella only (exact). Do NOT treat every pymergetic.* as a
     * host face — guest packs live under pymergetic.wasmmod_examples.*. */
    if (strcmp(dotted_name, "pymergetic") == 0) {
        return true;
    }
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

mp_obj_t mp_wasm_path_obj(void) {
    if (MP_STATE_VM(mp_wasm_path) == MP_OBJ_NULL) {
        MP_STATE_VM(mp_wasm_path) = mp_obj_new_list(0, NULL);
    }
    return MP_STATE_VM(mp_wasm_path);
}

void mp_wasm_path_append(const char *root) {
    if (root == NULL || root[0] == '\0') {
        return;
    }
    mp_obj_t lst = mp_wasm_path_obj();
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(lst, &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (mp_obj_is_str(items[i]) && strcmp(mp_obj_str_get_str(items[i]), root) == 0) {
            return;
        }
    }
    mp_obj_list_append(lst, mp_obj_new_str(root, strlen(root)));
}

static void dotted_to_slash(const char *dotted, vstr_t *out) {
    vstr_init(out, strlen(dotted) + 1);
    for (const char *p = dotted; *p; ++p) {
        vstr_add_char(out, *p == '.' ? '/' : (char)*p);
    }
}

static bool join_try_file(const char *root, const char *rel, vstr_t *path_out) {
    vstr_t path;
    size_t rlen = root ? strlen(root) : 0;
    size_t rel_len = strlen(rel);
    vstr_init(&path, rlen + rel_len + 2);
    if (rlen > 0) {
        vstr_add_strn(&path, root, rlen);
        if (root[rlen - 1] != '/') {
            vstr_add_char(&path, '/');
        }
    }
    vstr_add_strn(&path, rel, rel_len);
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

/* wait=async — HEAD (or fetch-synthesize) via io_ops. */
static bool try_url_candidate(const char *root, const char *rel, vstr_t *path_out) {
    size_t cap = strlen(root) + strlen(rel) + 2;
    char *uri = m_new(char, cap);
    pm_wasmmod_io_join_uri(root, rel, uri, (uint32_t)cap);
    int32_t ok = pm_wasmmod_io_probe(uri);
    if (ok) {
        vstr_init(path_out, strlen(uri) + 1);
        vstr_add_str(path_out, uri);
    }
    m_del(char, uri, cap);
    return ok != 0;
}

static bool try_rel(const char *root, const char *rel, vstr_t *path_out) {
    if (pm_wasmmod_io_uri_is_http(root)) {
        return try_url_candidate(root, rel, path_out);
    }
    return join_try_file(root, rel, path_out);
}

/* Prefer rel+".zlib" when present. */
static bool try_rel_prefer_zlib(const char *root, const char *rel, vstr_t *path_out) {
    vstr_t zrel;
    vstr_init(&zrel, strlen(rel) + 6);
    vstr_add_str(&zrel, rel);
    vstr_add_str(&zrel, ".zlib");
    if (try_rel(root, vstr_null_terminated_str(&zrel), path_out)) {
        vstr_clear(&zrel);
        return true;
    }
    vstr_clear(&zrel);
    return try_rel(root, rel, path_out);
}

static bool try_stem_ext(const char *root, const char *stem, const char *ext, vstr_t *path_out) {
    vstr_t rel;
    vstr_init(&rel, strlen(stem) + strlen(ext) + 1);
    vstr_add_str(&rel, stem);
    vstr_add_str(&rel, ext);
    bool ok = try_rel_prefer_zlib(root, vstr_null_terminated_str(&rel), path_out);
    vstr_clear(&rel);
    return ok;
}

static bool try_pkg_init(const char *root, const char *stem, const char *ext, vstr_t *path_out) {
    vstr_t rel;
    vstr_init(&rel, strlen(stem) + strlen(ext) + 14);
    vstr_add_str(&rel, stem);
    vstr_add_str(&rel, "/__init__");
    vstr_add_str(&rel, ext);
    bool ok = try_rel_prefer_zlib(root, vstr_null_terminated_str(&rel), path_out);
    vstr_clear(&rel);
    return ok;
}

static bool try_one_ext(const char *root, const char *dotted, const char *slash, const char *ext,
    vstr_t *path_out) {
    if (try_stem_ext(root, dotted, ext, path_out)) {
        return true;
    }
    if (try_stem_ext(root, slash, ext, path_out)) {
        return true;
    }
    if (try_pkg_init(root, slash, ext, path_out)) {
        return true;
    }
    return false;
}

static void aot_ext(char *buf, size_t buflen) {
    unsigned v = (unsigned)MICROPY_WASM_AOT_VERSION;
    if (v > 0) {
        snprintf(buf, buflen, ".aot%u", v);
    } else {
        snprintf(buf, buflen, ".aot");
    }
}

static bool try_aot(const char *root, const char *dotted, const char *slash, vstr_t *path_out) {
    char ext[16];
    aot_ext(ext, sizeof(ext));
    if (try_one_ext(root, dotted, slash, ext, path_out)) {
        return true;
    }
    if (strcmp(ext, ".aot") != 0) {
        return try_one_ext(root, dotted, slash, ".aot", path_out);
    }
    return false;
}

static bool try_candidates(const char *root, const char *dotted, const char *slash,
    vstr_t *path_out) {
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
            if (try_one_ext(root, dotted, slash, ".elf", path_out)) {
                return true;
            }
#endif
        } else if (n == 3 && memcmp(start, "aot", 3) == 0) {
            if (try_aot(root, dotted, slash, path_out)) {
                return true;
            }
        } else if (n == 4 && memcmp(start, "wasm", 4) == 0) {
            if (try_one_ext(root, dotted, slash, ".wasm", path_out)) {
                return true;
            }
        }
    }
    /* Fallback if CONTAINERS empty/odd: wasm only. */
    return try_one_ext(root, dotted, slash, ".wasm", path_out);
}

static bool find_in_list(mp_obj_t list_obj, const char *dotted, const char *slash,
    vstr_t *path_out) {
    if (list_obj == MP_OBJ_NULL) {
        return false;
    }
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(list_obj, &n, &items);
    for (size_t i = 0; i < n; ++i) {
        if (!mp_obj_is_str(items[i])) {
            continue;
        }
        const char *root = mp_obj_str_get_str(items[i]);
        if (strcmp(root, ".frozen") == 0) {
            continue;
        }
        /* Metal-cdn bases are artifacts/ only — probing {base}/name.wasm 200s HTML. */
        if (pm_wasmmod_io_uri_is_http(root)
            && pm_wasmmod_net_cdn_driver() == PM_WASMMOD_NET_CDN_DRIVER_METAL
            && pm_wasmmod_net_cdn_url_is_base(root)) {
            continue;
        }
        if (try_candidates(root, dotted, slash, path_out)) {
            return true;
        }
    }
    return false;
}

bool mp_wasm_find_pack(const char *dotted_name, vstr_t *path_out) {
    if (dotted_name == NULL || dotted_name[0] == '\0' || mp_wasm_is_host_face(dotted_name)) {
        return false;
    }
    if (local_pack_path(dotted_name, path_out)) {
        return true;
    }
    vstr_t slash;
    dotted_to_slash(dotted_name, &slash);
    const char *slash_name = vstr_null_terminated_str(&slash);

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

bool mp_wasm_read_file(const char *path, uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    if (path != NULL && strncmp(path, "mem:", 4) == 0) {
        const mp_wasm_local_pack_t *p = local_pack_by_name(path + 4);
        if (p == NULL) {
            return false;
        }
        /* Loader copies; do not m_new a second buffer (firmware GC-off
         * bump has truncated this once). unload must not m_del the bake. */
        *out = (uint8_t *)(uintptr_t)p->bytes;
        *out_len = p->len;
        return p->len > 0;
    }
    if (pm_wasmmod_io_uri_is_http(path)) {
        uint8_t *buf = NULL;
        uint32_t len = 0;
        char err[160];
        if (pm_wasmmod_io_fetch(path, &buf, &len, err, sizeof(err)) != 0 || buf == NULL) {
            return false;
        }
        uint8_t *mem = m_new(uint8_t, len ? len : 1);
        if (len > 0) {
            memcpy(mem, buf, len);
        }
        io_fetch_free(buf);
        *out = mem;
        *out_len = len;
        return len > 0;
    }
#if MICROPY_VFS
    mp_obj_t path_obj = mp_obj_new_str(path, strlen(path));
    mp_obj_t open_args[2] = { path_obj, MP_OBJ_NEW_QSTR(MP_QSTR_rb) };
    mp_obj_t f = mp_vfs_open(2, open_args, (mp_map_t *)&mp_const_empty_map);
    const mp_stream_p_t *stream = mp_get_stream_raise(f, MP_STREAM_OP_READ);

    vstr_t buf;
    vstr_init(&buf, 4096);
    uint8_t chunk[1024];
    for (;;) {
        int err = 0;
        mp_uint_t n = stream->read(f, chunk, sizeof(chunk), &err);
        if (n == MP_STREAM_ERROR) {
            vstr_clear(&buf);
            mp_raise_OSError(err);
        }
        if (n == 0) {
            break;
        }
        vstr_add_strn(&buf, (const char *)chunk, n);
    }
    mp_stream_close(f);

    if (buf.len == 0) {
        vstr_clear(&buf);
        return false;
    }
    uint8_t *mem = m_new(uint8_t, buf.len);
    memcpy(mem, buf.buf, buf.len);
    *out = mem;
    *out_len = buf.len;
    vstr_clear(&buf);
    return true;
#else
    (void)path;
    return false;
#endif
}

void mp_wasm_store_handle_on_module(mp_obj_t mod, pm_wasmmod_registry_handle_t h) {
    mp_obj_t g = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod));
    mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___wasm_h_index__),
        mp_obj_new_int_from_uint(h.index));
    mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___wasm_h_gen__),
        mp_obj_new_int_from_uint(h.generation));
}

bool mp_wasm_load_handle_from_module(mp_obj_t mod, pm_wasmmod_registry_handle_t *out) {
    mp_obj_t g = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod));
    mp_map_elem_t *ei = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(g))->map,
        MP_OBJ_NEW_QSTR(MP_QSTR___wasm_h_index__), MP_MAP_LOOKUP);
    mp_map_elem_t *eg = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(g))->map,
        MP_OBJ_NEW_QSTR(MP_QSTR___wasm_h_gen__), MP_MAP_LOOKUP);
    if (ei == NULL || eg == NULL || ei->value == MP_OBJ_NULL || eg->value == MP_OBJ_NULL) {
        return false;
    }
    out->index = (uint32_t)mp_obj_get_int(ei->value);
    out->generation = (uint32_t)mp_obj_get_int(eg->value);
    return true;
}

/* Unwrap MPZL in-place into a fresh m_new buffer when needed. */
static void unwrap_artifact(uint8_t **bytes, size_t *blen) {
    const uint8_t *p = *bytes;
    uint32_t len = (uint32_t)*blen;
    uint8_t *owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&p, &len, &owned)) {
        mp_raise_ValueError(MP_ERROR_TEXT("corrupt MPZL artifact"));
    }
    if (owned != NULL) {
        pack_bytes_free(*bytes, *blen);
        /* Move malloc'd inflate into GC heap so unload path is uniform. */
        uint8_t *mem = m_new(uint8_t, len);
        memcpy(mem, owned, len);
        MICROPY_WASM_FREE(owned);
        *bytes = mem;
        *blen = len;
    } else if (p != *bytes) {
        /* Shouldn't happen without owned. */
    }
}

static int mp_wasm_import_depth;

/* Local MPWD + metal-cdn: import_pack resolves deps (finder, then cdn). */
static void load_local_deps(const uint8_t *bytes, uint32_t blen) {
    if (mp_wasm_import_depth > 16) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm deps: nest limit"));
    }
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (!mp_wasm_deps_find_section(bytes, blen, &payload, &payload_len)) {
        return;
    }
    mp_wasm_deps_info_t deps;
    memset(&deps, 0, sizeof(deps));
    if (!mp_wasm_deps_parse(payload, payload_len, &deps)) {
        return;
    }
    for (uint32_t i = 0; i < deps.n_deps; ++i) {
        if (deps.deps[i].name_len == 0) {
            continue;
        }
        vstr_t name;
        vstr_init(&name, deps.deps[i].name_len + 1);
        vstr_add_strn(&name, deps.deps[i].name, deps.deps[i].name_len);
        const char *dep = vstr_null_terminated_str(&name);
        if (!pm_wasmmod_registry_has((const uint8_t *)dep, (uint32_t)strlen(dep))) {
            mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
                MP_OBJ_NEW_QSTR(qstr_from_str(dep)), MP_MAP_LOOKUP);
            if (el == NULL || el->value == MP_OBJ_NULL) {
                (void)mp_wasm_import_pack(dep);
            }
        }
        /* Version pin check (exact / semver / >= / ^ via util.version).
         * Empty / "*" pins are always satisfied. Any other pin requires a
         * registered version on the dep — no soft skip. */
        if (deps.deps[i].version_len > 0
            && !(deps.deps[i].version_len == 1 && deps.deps[i].version[0] == '*')) {
            uint8_t have_buf[64];
            uint32_t have_len = sizeof(have_buf);
            if (!pm_wasmmod_registry_version((const uint8_t *)dep, (uint32_t)strlen(dep),
                    have_buf, &have_len)
                || have_len == 0) {
                mp_wasm_deps_info_free(&deps);
                mp_raise_msg_varg(&mp_type_ImportError,
                    MP_ERROR_TEXT("wasm dep '%s' has no registered version"), dep);
            }
            int32_t ok = pm_util_version_satisfies(have_buf, have_len,
                (const uint8_t *)deps.deps[i].version, deps.deps[i].version_len);
            if (ok == 0) {
                mp_wasm_deps_info_free(&deps);
                mp_raise_msg_varg(&mp_type_ImportError,
                    MP_ERROR_TEXT("wasm dep '%s' version mismatch"), dep);
            } else if (ok < 0) {
                mp_wasm_deps_info_free(&deps);
                mp_raise_msg_varg(&mp_type_ImportError,
                    MP_ERROR_TEXT("wasm dep '%s' version unparsable"), dep);
            }
        }
        vstr_clear(&name);
    }
    mp_wasm_deps_info_free(&deps);
}

mp_obj_t mp_wasm_import_pack(const char *dotted_name) {
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(dotted_name)), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        return el->value;
    }
    if (mp_wasm_is_host_face(dotted_name)) {
        mp_raise_msg_varg(&mp_type_ImportError,
            MP_ERROR_TEXT("host face '%s' is not a guest pack"), dotted_name);
    }

    vstr_t path;
    uint8_t *bytes = NULL;
    size_t blen = 0;
    if (mp_wasm_find_pack(dotted_name, &path)) {
        const char *cpath = vstr_null_terminated_str(&path);
        if (!mp_wasm_read_file(cpath, &bytes, &blen)) {
            vstr_clear(&path);
            mp_raise_OSError(MP_ENOENT);
        }
        vstr_clear(&path);
    } else if (pm_wasmmod_net_cdn_driver() == PM_WASMMOD_NET_CDN_DRIVER_METAL) {
        uint8_t *buf = NULL;
        uint32_t n = 0;
        char err[160];
        if (pm_wasmmod_net_cdn_fetch_pack(dotted_name, NULL, &buf, &n, err, sizeof(err)) != 0) {
            mp_raise_msg_varg(&mp_type_ImportError,
                MP_ERROR_TEXT("no pack for '%s' (%s)"), dotted_name, err);
        }
        bytes = m_new(uint8_t, n ? n : 1);
        if (n > 0) {
            memcpy(bytes, buf, n);
        }
        io_fetch_free(buf);
        blen = n;
    } else {
        mp_raise_msg_varg(&mp_type_ImportError,
            MP_ERROR_TEXT("no pack for '%s'"), dotted_name);
    }

    unwrap_artifact(&bytes, &blen);

#if MICROPY_WASM_VERIFY
    {
        char err[160];
        if (!mp_wasm_verify_bytes(bytes, (uint32_t)blen, dotted_name, err, sizeof(err))) {
            pack_bytes_free(bytes, blen);
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("wasm verify: %s"), err);
        }
    }
#endif

    mp_wasm_import_depth++;
    nlr_buf_t nlr_deps;
    if (nlr_push(&nlr_deps) == 0) {
        load_local_deps(bytes, (uint32_t)blen);
        nlr_pop();
        mp_wasm_import_depth--;
    } else {
        mp_wasm_import_depth--;
        pack_bytes_free(bytes, blen);
        nlr_jump(nlr_deps.ret_val);
    }

    mp_wasm_artifact_kind_t kind = mp_wasm_artifact_kind(bytes, (uint32_t)blen);
    pm_wasmmod_registry_handle_t h = { .index = UINT32_MAX, .generation = 0 };

#if MICROPY_PY_WASM_ELF
    if (kind == MP_WASM_KIND_ELF) {
        char err[160];
        void *img = NULL;
        h = mp_wasm_elf_publish(dotted_name, bytes, (uint32_t)blen, &img, err, sizeof(err));
        if (h.index == UINT32_MAX) {
            pack_bytes_free(bytes, blen);
            mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("elf load: %s"), err);
        }
        mp_obj_t mod = mp_wasm_pack_bind(dotted_name, h, bytes, (uint32_t)blen);
        mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
            MP_OBJ_NEW_QSTR(MP_QSTR___wasm_elf__),
            mp_obj_new_int_from_ull((uint64_t)(uintptr_t)img));
        pack_bytes_free(bytes, blen);
        return mod;
    }
#else
    if (kind == MP_WASM_KIND_ELF) {
        pack_bytes_free(bytes, blen);
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("ELF disabled (MICROPY_PY_WASM_ELF=0)"));
    }
#endif

    h = pm_wasmmod_loader_load((const uint8_t *)dotted_name, (uint32_t)strlen(dotted_name),
        bytes, (uint32_t)blen);
    if (h.index == UINT32_MAX) {
        pack_bytes_free(bytes, blen);
        mp_raise_OSError(MP_EINVAL);
    }

    mp_obj_t mod = mp_wasm_pack_bind(dotted_name, h, bytes, (uint32_t)blen);
    pack_bytes_free(bytes, blen);
    return mod;
}

void mp_wasm_unload_pack(const char *dotted_name) {
    if (dotted_name == NULL || dotted_name[0] == '\0') {
        return;
    }
    size_t nlen = strlen(dotted_name);
    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;

    mp_map_elem_t *el = mp_map_lookup(map, MP_OBJ_NEW_QSTR(qstr_from_str(dotted_name)),
        MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
#if MICROPY_PY_WASM_ELF
        mp_obj_t g = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(el->value));
        mp_map_elem_t *ee = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(g))->map,
            MP_OBJ_NEW_QSTR(MP_QSTR___wasm_elf__), MP_MAP_LOOKUP);
        if (ee != NULL && ee->value != MP_OBJ_NULL) {
            mp_wasm_elf_release_for_module(el->value);
        } else
#endif
        {
            pm_wasmmod_registry_handle_t h;
            if (mp_wasm_load_handle_from_module(el->value, &h)) {
                (void)pm_wasmmod_loader_unload(h);
            }
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
        if ((klen == nlen && memcmp(k, dotted_name, nlen) == 0)
            || (klen > nlen && k[nlen] == '.' && memcmp(k, dotted_name, nlen) == 0)) {
            keys[nrem++] = map->table[i].key;
        }
    }
    for (size_t i = 0; i < nrem; ++i) {
        mp_map_lookup(map, keys[i], MP_MAP_LOOKUP_REMOVE_IF_FOUND);
    }
    m_del(mp_obj_t, keys, map->alloc);
}
