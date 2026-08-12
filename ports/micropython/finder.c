/*
 * Pack path finder — see finder.h.
 */

#include "ports/micropython/finder.h"

#include <string.h>

#include "extmod/vfs.h"
#include "py/builtin.h"
#include "py/mperrno.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/stream.h"

#if !MICROPY_PY_SYS_PATH
#error "wasmmod pack finder requires MICROPY_PY_SYS_PATH"
#endif

#include "src/pymergetic/wasmmod/loader/__exports__.h"
#include "src/pymergetic/wasmmod/registry/__exports__.h"

MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_path_obj);

bool mp_wasm_is_host_face(const char *dotted_name) {
    if (dotted_name == NULL || dotted_name[0] == '\0') {
        return false;
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
    if (MP_STATE_VM(mp_wasm_path_obj) == MP_OBJ_NULL) {
        MP_STATE_VM(mp_wasm_path_obj) = mp_obj_new_list(0, NULL);
    }
    return MP_STATE_VM(mp_wasm_path_obj);
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

static bool try_candidates(const char *root, const char *dotted, const char *slash,
    vstr_t *path_out) {
    vstr_t rel;
    /* flat: dotted.wasm */
    vstr_init(&rel, strlen(dotted) + 6);
    vstr_add_str(&rel, dotted);
    vstr_add_str(&rel, ".wasm");
    if (join_try_file(root, vstr_null_terminated_str(&rel), path_out)) {
        vstr_clear(&rel);
        return true;
    }
    vstr_clear(&rel);

    /* slash.wasm */
    vstr_init(&rel, strlen(slash) + 6);
    vstr_add_str(&rel, slash);
    vstr_add_str(&rel, ".wasm");
    if (join_try_file(root, vstr_null_terminated_str(&rel), path_out)) {
        vstr_clear(&rel);
        return true;
    }
    vstr_clear(&rel);

    /* slash/__init__.wasm */
    vstr_init(&rel, strlen(slash) + 14);
    vstr_add_str(&rel, slash);
    vstr_add_str(&rel, "/__init__.wasm");
    if (join_try_file(root, vstr_null_terminated_str(&rel), path_out)) {
        vstr_clear(&rel);
        return true;
    }
    vstr_clear(&rel);
    return false;
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
    /* close best-effort */
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
}

/* Namespace parents so µPy's level-walk can see the leaf in sys.modules.
 * Skip built-ins / host faces so we never shadow pymergetic.wasmmod. */
static void ensure_parent_packages(const char *full_name) {
    size_t len = strlen(full_name);
    for (size_t i = 0; i < len; ++i) {
        if (full_name[i] != '.') {
            continue;
        }
        qstr parent = qstr_from_strn(full_name, i);
        const char *pname = qstr_str(parent);
        if (mp_wasm_is_host_face(pname)) {
            continue;
        }
        if (mp_module_get_builtin(parent, false) != MP_OBJ_NULL) {
            continue;
        }
#if MICROPY_HAVE_REGISTERED_EXTENSIBLE_MODULES
        if (mp_module_get_builtin(parent, true) != MP_OBJ_NULL) {
            continue;
        }
#endif
        mp_obj_t pmod = mp_obj_new_module(parent);
        mp_obj_t g = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(pmod));
        mp_map_elem_t *el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(g))->map,
            MP_OBJ_NEW_QSTR(MP_QSTR___path__), MP_MAP_LOOKUP);
        if (el == NULL) {
            /* µPy packages use a str __path__ (not a list). */
            mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                mp_obj_new_str(pname, i));
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
    if (pel == NULL || cel == NULL || pel->value == MP_OBJ_NULL || cel->value == MP_OBJ_NULL) {
        return;
    }
    /* Built-in parents have fixed ROM globals — skip attribute link. */
    mp_obj_module_t *pmod = MP_OBJ_TO_PTR(pel->value);
    if (pmod->globals->map.is_fixed) {
        return;
    }
    mp_obj_dict_store(MP_OBJ_FROM_PTR(pmod->globals), MP_OBJ_NEW_QSTR(qleaf), cel->value);
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
    if (!mp_wasm_find_pack(dotted_name, &path)) {
        mp_raise_msg_varg(&mp_type_ImportError,
            MP_ERROR_TEXT("no pack for '%s'"), dotted_name);
    }
    const char *cpath = vstr_null_terminated_str(&path);
    uint8_t *bytes = NULL;
    size_t blen = 0;
    if (!mp_wasm_read_file(cpath, &bytes, &blen)) {
        vstr_clear(&path);
        mp_raise_OSError(MP_ENOENT);
    }
    vstr_clear(&path);

    size_t flen = strlen(dotted_name);
    pm_wasmmod_registry_handle_t h = pm_wasmmod_loader_load(
        (const uint8_t *)dotted_name, (uint32_t)flen, bytes, (uint32_t)blen);
    m_del(uint8_t, bytes, blen);
    if (h.index == UINT32_MAX) {
        mp_raise_OSError(MP_EINVAL);
    }

    ensure_parent_packages(dotted_name);
    qstr qn = qstr_from_strn(dotted_name, flen);
    mp_obj_t mod = mp_obj_new_module(qn);
    mp_wasm_store_handle_on_module(mod, h);
    link_module_to_parent(dotted_name);
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
        pm_wasmmod_registry_handle_t h;
        if (mp_wasm_load_handle_from_module(el->value, &h)) {
            (void)pm_wasmmod_loader_unload(h);
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
