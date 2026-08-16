/*
 * µPy __import__ wrap: pack-on-path before/after ImportError; presence → registry.
 */

#include <string.h>

#include "py/builtin.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/qstr.h"
#include "py/runtime.h"

#include "ports/common/boot.h"
#include "ports/micropython/finder.h"
#include "ports/micropython/hostready.h"
#include "ports/micropython/importhook.h"
#include "pymergetic/wasmmod/__version__.h"
#include "pymergetic/wasmmod/net/cdn.h"

#ifndef MICROPY_WASM_VERSION
#define MICROPY_WASM_VERSION PYMERGETIC_WASMMOD_VERSION
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_prev_import);

static int mp_wasm_hook_depth;
static int mp_wasm_inited;

void mp_wasm_presence_publish(const char *name) {
    pm_wasmmod_host_presence_publish(name);
}

/* Presence + auto-ready (bind_py + attach typed funobjs) for pymergetic.* leaves. */
static void mp_wasm_after_import(const char *name) {
    mp_wasm_presence_publish(name);
    if (name == NULL || strncmp(name, "pymergetic.", 11) != 0) {
        return;
    }
    /* Skip package shells; ready leaves (and namespace dirs with exports). */
    if (strcmp(name, "pymergetic.util") == 0 || strcmp(name, "pymergetic.wasmmod") == 0) {
        return;
    }
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        mp_wasm_host_ready(name, el->value);
    }
}

static void pm_wasm_sync_sys_modules_to_registry(void) {
    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    for (size_t i = 0; i < map->alloc; ++i) {
        if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_qstr(map->table[i].key)) {
            continue;
        }
        mp_wasm_presence_publish(qstr_str(MP_OBJ_QSTR_VALUE(map->table[i].key)));
    }
}

typedef struct _mp_wasm_walk_t {
    mp_obj_base_t base;
    mp_obj_t child;
    uint16_t nlen;
    char name[];
} mp_wasm_walk_t;

static void mp_wasm_walk_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_wasm_walk_t *w = MP_OBJ_TO_PTR(self_in);
    if (dest[0] != MP_OBJ_NULL) {
        return;
    }
    size_t alen;
    const byte *an = qstr_data(attr, &alen);
    if (alen == (size_t)w->nlen && memcmp(an, w->name, alen) == 0) {
        dest[0] = w->child;
    }
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_walk,
    MP_QSTR_module,
    MP_TYPE_FLAG_NONE,
    attr, mp_wasm_walk_attr
);

/* Guest pack lives under a ROM host package (pymergetic.*). Vanilla
 * __import__ walks that ROM dict and misses the namespace shell. Finish
 * here: sys.modules under the IMPORT_NAME qstr, return top or leaf like
 * CPython (`import a.b.c` → a, `from a.b.c import x` → leaf). */
static mp_obj_t mp_wasm_finish_pack_import(size_t n_args, const mp_obj_t *args, mp_obj_t leaf) {
    size_t n;
    const char *s = mp_obj_str_get_data(args[0], &n);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)), args[0], leaf);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
        MP_OBJ_NEW_QSTR(qstr_from_strn(s, n)), leaf);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] != '.') {
            continue;
        }
        qstr pq = qstr_from_strn(s, i);
        if (mp_wasm_is_host_face(qstr_str(pq))) {
            continue;
        }
        mp_obj_t pmod = mp_obj_new_module(pq);
        mp_obj_module_t *mod = MP_OBJ_TO_PTR(pmod);
        if (mod->globals->map.is_fixed) {
            continue;
        }
        mp_obj_t g = MP_OBJ_FROM_PTR(mod->globals);
        if (mp_map_lookup(&mod->globals->map, MP_OBJ_NEW_QSTR(MP_QSTR___path__), MP_MAP_LOOKUP)
            == NULL) {
            mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___path__), mp_obj_new_str(s, i));
        }
    }
    /* Leaf on its parent namespace (skip ROM host). */
    {
        const char *dot = NULL;
        for (size_t i = 0; i < n; ++i) {
            if (s[i] == '.') {
                dot = s + i;
            }
        }
        if (dot != NULL) {
            qstr pq = qstr_from_strn(s, (size_t)(dot - s));
            if (!mp_wasm_is_host_face(qstr_str(pq))) {
                mp_obj_t pmod = mp_obj_new_module(pq);
                mp_obj_module_t *mod = MP_OBJ_TO_PTR(pmod);
                if (!mod->globals->map.is_fixed) {
                    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod->globals),
                        MP_OBJ_NEW_QSTR(qstr_from_strn(dot + 1,
                            (size_t)((s + n) - (dot + 1)))), leaf);
                }
            }
        }
    }
    mp_obj_t fromlist = n_args >= 4 ? args[3] : mp_const_none;
    if (fromlist != mp_const_none && fromlist != mp_const_false) {
        return leaf;
    }
    /* `import a.b.c as d` LOAD_ATTRs remaining names. Do not return ROM
     * `a` (fixed dict) and do not key a heap module by interned qstr —
     * UEFI qstr ids for a slice of the import name can miss the compiler
     * intern. Walk objects strcmp the component bytes. */
    const char *parts[8];
    size_t lens[8];
    size_t nparts = 0;
    size_t start = 0;
    for (size_t k = 0; k <= n; ++k) {
        if (k < n && s[k] != '.') {
            continue;
        }
        if (nparts < 8) {
            parts[nparts] = s + start;
            lens[nparts] = k - start;
            nparts++;
        }
        start = k + 1;
    }
    if (nparts < 2) {
        return leaf;
    }
    mp_obj_t cur = leaf;
    for (size_t p = nparts; p > 1;) {
        --p;
        mp_wasm_walk_t *w = m_new_obj_var(mp_wasm_walk_t, name, char, lens[p] + 1);
        w->base.type = &mp_type_wasm_walk;
        w->child = cur;
        w->nlen = (uint16_t)lens[p];
        memcpy(w->name, parts[p], lens[p]);
        w->name[lens[p]] = '\0';
        cur = MP_OBJ_FROM_PTR(w);
    }
    return cur;
}

#if MICROPY_MODULE_ATTR_DELEGATION
void mp_wasm_pymergetic_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    (void)self_in;
    if (dest[0] != MP_OBJ_NULL) {
        return;
    }
    const char *leaf = qstr_str(attr);
    size_t llen = strlen(leaf);
    vstr_t name;
    vstr_init(&name, 11 + llen);
    vstr_add_strn(&name, "pymergetic.", 11);
    vstr_add_strn(&name, leaf, llen);
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_strn(name.buf, name.len)), MP_MAP_LOOKUP);
    vstr_clear(&name);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        dest[0] = el->value;
    }
}
#endif

mp_obj_t mp_wasm_builtin_import(size_t n_args, const mp_obj_t *args) {
    if (mp_wasm_hook_depth == 0 && n_args >= 1 && mp_obj_is_str(args[0])) {
        const char *name = mp_obj_str_get_str(args[0]);
        mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
            MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
        if (el == NULL || el->value == MP_OBJ_NULL) {
            vstr_t path;
            if (mp_wasm_find_pack(name, &path)) {
                vstr_clear(&path);
                mp_wasm_hook_depth++;
                nlr_buf_t nlr_pack;
                if (nlr_push(&nlr_pack) == 0) {
                    mp_obj_t pack = mp_wasm_import_pack(name);
                    nlr_pop();
                    mp_wasm_hook_depth--;
                    mp_obj_t res = mp_wasm_finish_pack_import(n_args, args, pack);
                    mp_wasm_after_import(name);
                    return res;
                }
                mp_wasm_hook_depth--;
                nlr_jump(nlr_pack.ret_val);
            }
        }
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        /* Never call mp_builtin___import___obj — with the compile-time
         * wrap that funobj is this function. */
        mp_obj_t res = mp_builtin___import___default(n_args, args);
        nlr_pop();
        if (mp_wasm_hook_depth == 0 && n_args >= 1 && mp_obj_is_str(args[0])) {
            mp_wasm_after_import(mp_obj_str_get_str(args[0]));
        }
        return res;
    }

    mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
    if (!mp_obj_exception_match(exc, MP_OBJ_FROM_PTR(&mp_type_ImportError))
        || mp_wasm_hook_depth > 0 || n_args < 1 || !mp_obj_is_str(args[0])) {
        nlr_jump(nlr.ret_val);
    }

    const char *name = mp_obj_str_get_str(args[0]);
    mp_wasm_hook_depth++;
    nlr_buf_t nlr2;
    if (nlr_push(&nlr2) == 0) {
        mp_obj_t pack = mp_wasm_import_pack(name);
        nlr_pop();
        mp_wasm_hook_depth--;
        mp_obj_t res = mp_wasm_finish_pack_import(n_args, args, pack);
        mp_wasm_after_import(name);
        return res;
    }
    mp_wasm_hook_depth--;
    if (mp_obj_exception_match(MP_OBJ_FROM_PTR(nlr2.ret_val), MP_OBJ_FROM_PTR(&mp_type_ImportError))) {
        nlr_jump(nlr.ret_val);
    }
    nlr_jump(nlr2.ret_val);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_import_hook_obj, 1, 5, mp_wasm_builtin_import);

static mp_obj_t mod_wasm_install_hook(void) {
#if MICROPY_CAN_OVERRIDE_BUILTINS
    if (MP_STATE_VM(mp_wasm_prev_import) != MP_OBJ_NULL) {
        pm_wasm_sync_sys_modules_to_registry();
        return mp_const_none;
    }
    mp_obj_t dest[2];
    mp_load_method_maybe(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, dest);
    MP_STATE_VM(mp_wasm_prev_import) =
        dest[0] != MP_OBJ_NULL ? dest[0] : MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__,
        MP_OBJ_FROM_PTR(&mod_wasm_import_hook_obj));
    pm_wasm_sync_sys_modules_to_registry();
    return mp_const_none;
#else
    mp_raise_NotImplementedError(MP_ERROR_TEXT("install_hook needs MICROPY_CAN_OVERRIDE_BUILTINS"));
#endif
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_install_hook_obj, mod_wasm_install_hook);

void mp_wasm_ensure_inited(void) {
    if (!mp_wasm_inited) {
        /* Metal boots from pymergetic.metal.__init__ so PM_MOD_BOOT can queue first. */
        if (pm_wasmmod_host_boot("pymergetic.wasmmod", MICROPY_WASM_VERSION) != 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasmmod loader_init failed"));
        }
        mp_wasm_inited = 1;
        (void)mp_wasm_path_obj();
    }
    if (MP_STATE_VM(mp_wasm_prev_import) == MP_OBJ_NULL) {
        (void)mod_wasm_install_hook();
    }
}

static mp_obj_t mod_wasm_uninstall_hook(void) {
#if MICROPY_CAN_OVERRIDE_BUILTINS
    if (MP_STATE_VM(mp_wasm_prev_import) == MP_OBJ_NULL) {
        return mp_const_none;
    }
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__,
        MP_STATE_VM(mp_wasm_prev_import));
    MP_STATE_VM(mp_wasm_prev_import) = MP_OBJ_NULL;
#endif
    pm_wasmmod_net_cdn_reset();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_uninstall_hook_obj, mod_wasm_uninstall_hook);

static mp_obj_t mod_wasm_publish_presence(mp_obj_t name_in) {
    mp_wasm_ensure_inited();
    mp_wasm_presence_publish(mp_obj_str_get_str(name_in));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_publish_presence_obj, mod_wasm_publish_presence);
