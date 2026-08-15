/*
 * µPy __import__ wrap: pack-on-path before/after ImportError; presence → registry.
 */

#include <string.h>

#include "py/builtin.h"
#include "py/nlr.h"
#include "py/obj.h"
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

static mp_obj_t mod_wasm_import_hook(size_t n_args, const mp_obj_t *args) {
    mp_obj_t prev = MP_STATE_VM(mp_wasm_prev_import);
    if (prev == MP_OBJ_NULL) {
        prev = MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    }

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
                    (void)mp_wasm_import_pack(name);
                    nlr_pop();
                    mp_wasm_hook_depth--;
                    mp_obj_t res = mp_call_function_n_kw(prev, n_args, 0, args);
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
        mp_obj_t res = mp_call_function_n_kw(prev, n_args, 0, args);
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
        (void)mp_wasm_import_pack(name);
        nlr_pop();
        mp_wasm_hook_depth--;
        mp_obj_t res = mp_call_function_n_kw(prev, n_args, 0, args);
        mp_wasm_after_import(name);
        return res;
    }
    mp_wasm_hook_depth--;
    if (mp_obj_exception_match(MP_OBJ_FROM_PTR(nlr2.ret_val), MP_OBJ_FROM_PTR(&mp_type_ImportError))) {
        nlr_jump(nlr.ret_val);
    }
    nlr_jump(nlr2.ret_val);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_import_hook_obj, 1, 5, mod_wasm_import_hook);

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
        if (pm_metal_boot() != 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("metal boot failed"));
        }
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
