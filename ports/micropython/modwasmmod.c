/*
 * Thin MicroPython face for wasmmod (mpwm / micropython-wasmmod only).
 *
 * One-module law (rewrite APIs):
 *   - sys.modules = Python import face
 *   - pm_wasmmod_registry_* = native export table
 *   - install_hook auto-installed on first use / parent __init__
 *   - every successful import gets a presence publish (container=RESIDENT
 *     if not already a wasm/aot/elf entry)
 *
 * No host_slots / call0_py — resolve via pm_wasmmod_registry_resolve_native.
 *
 * Note: `import pymergetic.wasmmod` often loads wasmmod as an attribute of
 * the parent builtin without running the child's MICROPY_MODULE_BUILTIN_INIT
 * path — so we also put __init__ on `pymergetic` and call ensure_inited from
 * every public entry point.
 */

#include <string.h>

#include "py/builtin.h"
#include "py/mperrno.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

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

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_n_kw(prev, n_args, 0, args);
        nlr_pop();
        if (mp_wasm_hook_depth == 0 && n_args >= 1 && mp_obj_is_str(args[0])) {
            pm_wasm_presence_publish(mp_obj_str_get_str(args[0]));
        }
        return res;
    }

    /* ImportError: pack finder lands in loader/finder/ later. */
    nlr_jump(nlr.ret_val);
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
    }
    /* Soft-reset clears builtins override; always re-install if missing. */
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
    return mp_obj_new_module(qn);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_load_obj, 1, 2, mod_wasm_load);

static mp_obj_t mod_wasm_unload(mp_obj_t name_in) {
    pm_wasm_ensure_inited();
    const char *name = mp_obj_str_get_str(name_in);
    size_t nlen = strlen(name);

    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;
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

/* Parent package init — this is what actually runs on `import pymergetic…`. */
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

MP_REGISTER_MODULE(MP_QSTR_pymergetic, mp_module_pymergetic);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_wasmmod, mp_module_pymergetic_wasmmod);
