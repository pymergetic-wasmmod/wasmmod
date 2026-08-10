/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Face: sys.modules. Our metadata: global __pm_modules (same dict shape),
 * GC-rooted + builtins override. Face modules are never written for pm_mod.
 */

#include "pm_mod.h"

#include "extmod/wasmmod/alloc.h"

#include "py/builtin.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include <string.h>

#if MICROPY_PY_WASM
#include "extmod/wasmmod/forward.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/runtime.h"
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t pm_mod_modules_dict);

static mp_obj_t pm_mod_map_ensure(void) {
    if (MP_STATE_VM(pm_mod_modules_dict) == MP_OBJ_NULL) {
        MP_STATE_VM(pm_mod_modules_dict) = mp_obj_new_dict(16);
    }
    mp_obj_t map = MP_STATE_VM(pm_mod_modules_dict);
    qstr qg = qstr_from_str("__pm_modules");
#if MICROPY_CAN_OVERRIDE_BUILTINS
    if (MP_STATE_VM(mp_module_builtins_override_dict) == NULL) {
        MP_STATE_VM(mp_module_builtins_override_dict) = MP_OBJ_TO_PTR(mp_obj_new_dict(1));
    }
    mp_obj_dict_store(MP_OBJ_FROM_PTR(MP_STATE_VM(mp_module_builtins_override_dict)),
        MP_OBJ_NEW_QSTR(qg), map);
#else
#error "pm_mod requires MICROPY_CAN_OVERRIDE_BUILTINS for global __pm_modules"
#endif
    return map;
}

void pm_mod_init(void) {
    /* mp_init clears builtins override before PORT_INIT; root may be stale
     * after soft-reset — always publish a fresh empty map. */
    MP_STATE_VM(pm_mod_modules_dict) = MP_OBJ_NULL;
    (void)pm_mod_map_ensure();
}

static mp_obj_t pm_mod_record_ensure(const char *name) {
    mp_obj_t map = pm_mod_map_ensure();
    qstr qn = qstr_from_str(name);
    mp_map_elem_t *el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(map))->map,
        MP_OBJ_NEW_QSTR(qn), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL && mp_obj_is_dict_or_ordereddict(el->value)) {
        return el->value;
    }
    mp_obj_t rec = mp_obj_new_dict(4);
    mp_obj_dict_store(rec, MP_OBJ_NEW_QSTR(qstr_from_str("container")), MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_dict_store(rec, MP_OBJ_NEW_QSTR(qstr_from_str("native")), mp_obj_new_dict(8));
    mp_obj_dict_store(map, MP_OBJ_NEW_QSTR(qn), rec);
    return rec;
}

static mp_obj_t pm_mod_record_native_dict(mp_obj_t rec) {
    qstr qn = qstr_from_str("native");
    mp_map_elem_t *el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(rec))->map,
        MP_OBJ_NEW_QSTR(qn), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL && mp_obj_is_dict_or_ordereddict(el->value)) {
        return el->value;
    }
    mp_obj_t nat = mp_obj_new_dict(8);
    mp_obj_dict_store(rec, MP_OBJ_NEW_QSTR(qn), nat);
    return nat;
}

static mp_obj_t pm_mod_lookup_face(const char *name) {
    if (name == NULL || name[0] == 0) {
        return MP_OBJ_NULL;
    }
    qstr q = qstr_from_str(name);
    mp_map_elem_t *elem =
        mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map, MP_OBJ_NEW_QSTR(q), MP_MAP_LOOKUP);
    if (elem != NULL) {
        return elem->value;
    }
    mp_obj_t builtin = mp_module_get_builtin(q, false);
    if (builtin != MP_OBJ_NULL) {
        return builtin;
    }
    return mp_module_get_builtin(q, true);
}

int pm_mod_publish(const char *name, pm_mod_container_t container,
    const pm_mod_export_t *exports, uint32_t n_exports) {
    if (name == NULL || name[0] == 0) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return PM_ERR;
    }
    mp_obj_t rec = pm_mod_record_ensure(name);
    mp_obj_dict_store(rec, MP_OBJ_NEW_QSTR(qstr_from_str("container")),
        MP_OBJ_NEW_SMALL_INT((mp_int_t)container));
    mp_obj_t nat = pm_mod_record_native_dict(rec);
    if (exports != NULL) {
        for (uint32_t i = 0; i < n_exports; i++) {
            if (exports[i].name == NULL || exports[i].name[0] == 0) {
                continue;
            }
            mp_obj_dict_store(nat, MP_OBJ_NEW_QSTR(qstr_from_str(exports[i].name)),
                mp_obj_new_int_from_ull((unsigned long long)(uintptr_t)exports[i].ptr));
        }
    }
    nlr_pop();
    return PM_OK;
}

int pm_mod_unpublish(const char *name) {
    if (name == NULL || name[0] == 0) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return PM_ERR;
    }
    mp_obj_t map = MP_STATE_VM(pm_mod_modules_dict);
    if (map == MP_OBJ_NULL) {
        /* Never published (e.g. pm_mod_init hasn't run) — nothing to drop. */
        nlr_pop();
        return PM_OK;
    }
    /* Removes the whole record (container + native dict) in one shot.
     * Any slot-backed export (thunk-guest-exports / py-native-export
     * trampoline tables) must free its own slot before/while this runs —
     * this only drops the __pm_modules bookkeeping entry itself. */
    mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(map))->map,
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP_REMOVE_IF_FOUND);
    nlr_pop();
    return PM_OK;
}

int pm_mod_export_set(const char *module, const char *func, void *fn) {
    if (module == NULL || func == NULL) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return PM_ERR;
    }
    mp_obj_t rec = pm_mod_record_ensure(module);
    mp_obj_t nat = pm_mod_record_native_dict(rec);
    mp_obj_dict_store(nat, MP_OBJ_NEW_QSTR(qstr_from_str(func)),
        mp_obj_new_int_from_ull((unsigned long long)(uintptr_t)fn));
    nlr_pop();
    return PM_OK;
}

void *pm_mod_resolve_native(const char *module, const char *func) {
    if (module == NULL || func == NULL) {
        return NULL;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return NULL;
    }
    mp_obj_t map = MP_STATE_VM(pm_mod_modules_dict);
    if (map == MP_OBJ_NULL) {
        map = pm_mod_map_ensure();
    }
    mp_map_elem_t *rel = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(map))->map,
        MP_OBJ_NEW_QSTR(qstr_from_str(module)), MP_MAP_LOOKUP);
    if (rel == NULL || !mp_obj_is_dict_or_ordereddict(rel->value)) {
        nlr_pop();
        return NULL;
    }
    mp_obj_t nat = pm_mod_record_native_dict(rel->value);
    mp_map_elem_t *elem = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(nat))->map,
        MP_OBJ_NEW_QSTR(qstr_from_str(func)), MP_MAP_LOOKUP);
    if (elem == NULL || !mp_obj_is_int(elem->value)) {
        nlr_pop();
        return NULL;
    }
    mp_uint_t u = (mp_uint_t)mp_obj_get_int_truncated(elem->value);
    nlr_pop();
    return (void *)(uintptr_t)u;
}

int pm_mod_connect_import(pm_mod_import_t *imp) {
    if (imp == NULL || imp->module == NULL || imp->func == NULL) {
        return PM_ERR_ARG;
    }
    void *fn = pm_mod_resolve_native(imp->module, imp->func);
    imp->slot = fn;
    return fn != NULL ? PM_OK : PM_ERR;
}

void pm_mod_connect_imports(pm_mod_import_t *imps, uint32_t n) {
    if (imps == NULL) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        (void)pm_mod_connect_import(&imps[i]);
    }
}

void *pm_mod_import_get(pm_mod_import_t *imp) {
    if (imp == NULL) {
        return NULL;
    }
    if (imp->slot != NULL) {
        return imp->slot;
    }
    (void)pm_mod_connect_import(imp);
    return imp->slot;
}

bool pm_mod_has(const char *name) {
    if (name == NULL) {
        return false;
    }
    if (pm_mod_lookup_face(name) != MP_OBJ_NULL) {
        return true;
    }
    mp_obj_t map = MP_STATE_VM(pm_mod_modules_dict);
    if (map == MP_OBJ_NULL) {
        return false;
    }
    mp_map_elem_t *el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(map))->map,
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
    return el != NULL && el->value != MP_OBJ_NULL;
}

void *pm_mod_border_malloc(size_t n) {
    return MICROPY_WASM_MALLOC(n);
}

void pm_mod_border_free(void *p) {
    MICROPY_WASM_FREE(p);
}

void *pm_mod_border_realloc(void *p, size_t n) {
#ifdef MICROPY_WASM_REALLOC
    return MICROPY_WASM_REALLOC(p, n);
#else
    (void)p;
    return MICROPY_WASM_MALLOC(n);
#endif
}

int pm_mod_connect_guest(void *pack_instance) {
#if !MICROPY_PY_WASM
    (void)pack_instance;
    return PM_ERR_FEATURE;
#else
    mp_pack_t *pack = (mp_pack_t *)pack_instance;
    if (pack == NULL) {
        return PM_ERR_ARG;
    }
    /* Wire NEED/import peers: pack lifecycle list OR __pm_modules natives. */
    char err[160];
    if (!mp_wasm_pm_connect_guest(pack, err, sizeof(err))) {
        return PM_ERR;
    }
    return PM_OK;
#endif
}
