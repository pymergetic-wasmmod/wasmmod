/*
 * Thin MicroPython face for wasmmod (mpwm / micropython-wasmmod only).
 *
 * One-module law (rewrite APIs):
 *   - sys.modules = Python import face
 *   - pm_wasmmod_registry_* = native export table
 *   - install_hook auto-installed on first use / parent __init__
 *   - ImportError → pack finder (wasm.path / sys.path → .wasm)
 *   - unload(name) → pm_wasmmod_loader_unload when handle attrs present
 *
 * No host_slots / call0_py — resolve via pm_wasmmod_registry_resolve_native.
 */

#include <string.h>

#include "py/builtin.h"
#include "py/mperrno.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include "ports/micropython/finder.h"
#include "src/pymergetic/wasmmod/registry/__exports__.h"
#include "src/pymergetic/wasmmod/loader/__exports__.h"

#if !MICROPY_PY_WASM
#error "ports/micropython/modwasmmod.c requires MICROPY_PY_WASM=1"
#endif

#ifndef MICROPY_WASM_VERSION
#define MICROPY_WASM_VERSION "0.2.0a2"
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_prev_import);

static int mp_wasm_hook_depth;
static int mp_wasm_inited;

static void pm_wasm_presence_publish(const char *name) {
    if (name == NULL || name[0] == 0) {
        return;
    }
    size_t n = strlen(name);
    if (pm_wasmmod_registry_has((const uint8_t *)name, (uint32_t)n)) {
        return;
    }
    (void)pm_wasmmod_registry_publish((const uint8_t *)name, (uint32_t)n,
        PM_WASMMOD_REGISTRY_CONTAINER_RESIDENT);
}

static void pm_wasm_sync_sys_modules_to_registry(void) {
    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    for (size_t i = 0; i < map->alloc; ++i) {
        if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_qstr(map->table[i].key)) {
            continue;
        }
        pm_wasm_presence_publish(qstr_str(MP_OBJ_QSTR_VALUE(map->table[i].key)));
    }
}

static mp_obj_t mod_wasm_import_hook(size_t n_args, const mp_obj_t *args) {
    mp_obj_t prev = MP_STATE_VM(mp_wasm_prev_import);
    if (prev == MP_OBJ_NULL) {
        prev = MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    }

    /* Prefer a pack on wasm.path before delegating (beats empty dirs). */
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
                    pm_wasm_presence_publish(name);
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
            pm_wasm_presence_publish(mp_obj_str_get_str(args[0]));
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
        pm_wasm_presence_publish(name);
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
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_install_hook_obj, mod_wasm_install_hook);

static void pm_wasm_ensure_inited(void) {
    if (!mp_wasm_inited) {
        pm_wasmmod_registry_init();
        if (pm_wasmmod_loader_init() != 0) {
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
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_uninstall_hook_obj, mod_wasm_uninstall_hook);

static mp_obj_t mod_wasm_publish_presence(mp_obj_t name_in) {
    pm_wasm_ensure_inited();
    pm_wasm_presence_publish(mp_obj_str_get_str(name_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_publish_presence_obj, mod_wasm_publish_presence);

static mp_obj_t mod_wasm_has(mp_obj_t name_in) {
    pm_wasm_ensure_inited();
    const char *name = mp_obj_str_get_str(name_in);
    return mp_obj_new_bool(pm_wasmmod_registry_has((const uint8_t *)name, (uint32_t)strlen(name)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_has_obj, mod_wasm_has);

static mp_obj_t mod_wasm_version(void) {
    pm_wasm_ensure_inited();
    return mp_obj_new_str(MICROPY_WASM_VERSION, strlen(MICROPY_WASM_VERSION));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_version_obj, mod_wasm_version);

static mp_obj_t mod_wasm_path(void) {
    pm_wasm_ensure_inited();
    return mp_wasm_path_obj();
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_path_obj_fun, mod_wasm_path);

static mp_obj_t mod_wasm_path_append(mp_obj_t root_in) {
    pm_wasm_ensure_inited();
    mp_wasm_path_append(mp_obj_str_get_str(root_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_path_append_obj, mod_wasm_path_append);

static mp_obj_t mod_wasm_load(size_t n_args, const mp_obj_t *args) {
    pm_wasm_ensure_inited();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    const char *fqn = n_args > 1 ? mp_obj_str_get_str(args[1]) : "anon";
    size_t flen = strlen(fqn);

    pm_wasmmod_registry_handle_t h = pm_wasmmod_loader_load(
        (const uint8_t *)fqn, (uint32_t)flen,
        (const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len);
    if (h.index == UINT32_MAX) {
        mp_raise_OSError(MP_EINVAL);
    }

    qstr qn = qstr_from_strn(fqn, flen);
    mp_obj_t mod = mp_obj_new_module(qn);
    mp_wasm_store_handle_on_module(mod, h);
    return mod;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_load_obj, 1, 2, mod_wasm_load);

static mp_obj_t mod_wasm_unload(mp_obj_t name_in) {
    pm_wasm_ensure_inited();
    mp_wasm_unload_pack(mp_obj_str_get_str(name_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_unload_obj, mod_wasm_unload);

static mp_obj_t mod_wasm_call(size_t n_args, const mp_obj_t *args) {
    pm_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(args[0]);
    const char *exp = mp_obj_str_get_str(args[1]);
    pm_wasmmod_registry_value_t argv[8];
    uint32_t nargs = 0;
    if (n_args > 2) {
        size_t len;
        mp_obj_t *items;
        mp_obj_get_array(args[2], &len, &items);
        if (len > 8) {
            mp_raise_ValueError(MP_ERROR_TEXT("call: at most 8 args"));
        }
        for (size_t i = 0; i < len; ++i) {
            argv[i].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            argv[i].of.i32 = (int32_t)mp_obj_get_int(items[i]);
            nargs++;
        }
    }
    pm_wasmmod_registry_value_t results[1];
    memset(results, 0, sizeof(results));
    int32_t st = pm_wasmmod_registry_call(
        (const uint8_t *)fqn, (uint32_t)strlen(fqn),
        (const uint8_t *)exp, (uint32_t)strlen(exp),
        argv, nargs, results, 1);
    if (st < 0) {
        mp_raise_msg(&mp_type_LookupError, MP_ERROR_TEXT("registry call miss"));
    }
    if (results[0].kind == PM_WASMMOD_REGISTRY_VALKIND_I32) {
        return mp_obj_new_int(results[0].of.i32);
    }
    return mp_obj_new_int(st);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_call_obj, 2, 3, mod_wasm_call);

static mp_obj_t mod_wasm___init__(void) {
    pm_wasm_ensure_inited();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm___init___obj, mod_wasm___init__);

static mp_obj_t mod_pymergetic___init__(void) {
    pm_wasm_ensure_inited();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_pymergetic___init___obj, mod_pymergetic___init__);

static const mp_rom_map_elem_t mp_module_pymergetic_wasmmod_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_wasm___init___obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mod_wasm_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_has), MP_ROM_PTR(&mod_wasm_has_obj) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&mod_wasm_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_unload), MP_ROM_PTR(&mod_wasm_unload_obj) },
    { MP_ROM_QSTR(MP_QSTR_call), MP_ROM_PTR(&mod_wasm_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_path), MP_ROM_PTR(&mod_wasm_path_obj_fun) },
    { MP_ROM_QSTR(MP_QSTR_path_append), MP_ROM_PTR(&mod_wasm_path_append_obj) },
    { MP_ROM_QSTR(MP_QSTR_install_hook), MP_ROM_PTR(&mod_wasm_install_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_uninstall_hook), MP_ROM_PTR(&mod_wasm_uninstall_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish_presence), MP_ROM_PTR(&mod_wasm_publish_presence_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_wasmmod_globals, mp_module_pymergetic_wasmmod_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_wasmmod_globals,
};

static const mp_rom_map_elem_t mp_module_pymergetic_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_pymergetic___init___obj) },
    { MP_ROM_QSTR(MP_QSTR_wasmmod), MP_ROM_PTR(&mp_module_pymergetic_wasmmod) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_globals, mp_module_pymergetic_globals_table);

const mp_obj_module_t mp_module_pymergetic = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_globals,
};

#if MICROPY_MODULE_ATTR_DELEGATION
/* ROM pymergetic can't grow; resolve pack namespace children via sys.modules. */
void mp_module_pymergetic_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
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
MP_REGISTER_MODULE_DELEGATION(mp_module_pymergetic, mp_module_pymergetic_attr);
#endif

MP_REGISTER_MODULE(MP_QSTR_pymergetic, mp_module_pymergetic);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_wasmmod, mp_module_pymergetic_wasmmod);
