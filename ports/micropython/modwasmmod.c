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

/* This TU is only linked when MICROPY_PY_WASM=1 (micropython.mk). Default
 * here so clangd / isolated -fsyntax-only still see the host face when the
 * compile command omits -D; explicit -DMICROPY_PY_WASM=0 still #errors. */
#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (1)
#endif

#include <string.h>

#include "py/builtin.h"
#include "py/mperrno.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/stream.h"

#include "extmod/vfs.h"

#include "ports/micropython/finder.h"
#include "ports/micropython/mpconfig_wasm.h"
#include "ports/micropython/packbind.h"
#include "pymergetic/util/gen/__exports__.h"
#include "pymergetic/wasmmod/__version__.h"
#include "pymergetic/wasmmod/api/__exports__.h"
#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/format/common/format.h"
#include "pymergetic/wasmmod/pack/source.h"
#include "pymergetic/wasmmod/pack/zlib_env.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/verify/__exports__.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#ifndef MICROPY_WASM_VERSION
#define MICROPY_WASM_VERSION PYMERGETIC_WASMMOD_VERSION
#endif

#ifndef MICROPY_PY_WASM_GEN
#define MICROPY_PY_WASM_GEN (0)
#endif

#if !MICROPY_PY_WASM
#error "ports/micropython/modwasmmod.c requires MICROPY_PY_WASM=1"
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
        /* Kernel product version — registry is SoT; wasmmod.version reads it. */
        (void)pm_wasmmod_registry_ensure((const uint8_t *)"pymergetic.wasmmod",
            (uint32_t)strlen("pymergetic.wasmmod"), PM_WASMMOD_REGISTRY_CONTAINER_RESIDENT);
        (void)pm_wasmmod_registry_set_version((const uint8_t *)"pymergetic.wasmmod",
            (uint32_t)strlen("pymergetic.wasmmod"),
            (const uint8_t *)MICROPY_WASM_VERSION, (uint32_t)strlen(MICROPY_WASM_VERSION));
        mp_wasm_trust_init_session();
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
    uint8_t buf[64];
    uint32_t n = sizeof(buf);
    if (pm_wasmmod_registry_version((const uint8_t *)"pymergetic.wasmmod",
            (uint32_t)strlen("pymergetic.wasmmod"), buf, &n)
        && n > 0) {
        return mp_obj_new_str((const char *)buf, n);
    }
    return mp_obj_new_str(MICROPY_WASM_VERSION, strlen(MICROPY_WASM_VERSION));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_version_obj, mod_wasm_version);

/* wasmmod.test(fqn) → fail count; wasmmod.test(fqn, case) → case status (0 pass). */
static mp_obj_t mod_wasm_test(size_t n_args, const mp_obj_t *args) {
    pm_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(args[0]);
    size_t flen = strlen(fqn);
    if (n_args >= 2) {
        const char *name = mp_obj_str_get_str(args[1]);
        return mp_obj_new_int(pm_wasmmod_registry_test_run(
            (const uint8_t *)fqn, (uint32_t)flen,
            (const uint8_t *)name, (uint32_t)strlen(name)));
    }
    return mp_obj_new_int(pm_wasmmod_registry_test_run_all(
        (const uint8_t *)fqn, (uint32_t)flen));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_test_obj, 1, 2, mod_wasm_test);

static mp_obj_t mod_wasm_test_all(void) {
    pm_wasm_ensure_inited();
    uint32_t n = pm_wasmmod_registry_module_count();
    int32_t fails = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t buf[256];
        uint32_t len = sizeof(buf);
        if (!pm_wasmmod_registry_module_at(i, buf, &len) || len == 0) {
            continue;
        }
        if (pm_wasmmod_registry_test_count(buf, len) == 0) {
            continue;
        }
        fails += pm_wasmmod_registry_test_run_all(buf, len);
    }
    return mp_obj_new_int(fails);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_test_all_obj, mod_wasm_test_all);

static mp_obj_t mod_wasm_tests(mp_obj_t fqn_in) {
    pm_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    size_t flen = strlen(fqn);
    uint32_t tc = pm_wasmmod_registry_test_count((const uint8_t *)fqn, (uint32_t)flen);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (uint32_t i = 0; i < tc; i++) {
        uint8_t buf[128];
        uint32_t len = sizeof(buf);
        if (!pm_wasmmod_registry_test_at((const uint8_t *)fqn, (uint32_t)flen, i, buf, &len)
            || len == 0) {
            continue;
        }
        mp_obj_list_append(list, mp_obj_new_str((const char *)buf, len));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_tests_obj, mod_wasm_tests);

static mp_obj_t mod_wasm_test_count(mp_obj_t fqn_in) {
    pm_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    return mp_obj_new_int_from_uint(pm_wasmmod_registry_test_count(
        (const uint8_t *)fqn, (uint32_t)strlen(fqn)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_test_count_obj, mod_wasm_test_count);

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

    const uint8_t *p = (const uint8_t *)bufinfo.buf;
    uint32_t len = (uint32_t)bufinfo.len;
    uint8_t *owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&p, &len, &owned)) {
        mp_raise_ValueError(MP_ERROR_TEXT("corrupt MPZL artifact"));
    }

    char err[160];
    if (!mp_wasm_verify_bytes(p, len, fqn, err, sizeof(err))) {
        MICROPY_WASM_FREE(owned);
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("wasm verify: %s"), err);
    }

    pm_wasmmod_registry_handle_t h;
    mp_obj_t mod;
    mp_wasm_artifact_kind_t kind = mp_wasm_artifact_kind(p, len);
#if MICROPY_PY_WASM_ELF
    if (kind == MP_WASM_KIND_ELF) {
        void *img = NULL;
        h = mp_wasm_elf_publish(fqn, p, len, &img, err, sizeof(err));
        if (h.index == UINT32_MAX) {
            MICROPY_WASM_FREE(owned);
            mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("elf load: %s"), err);
        }
        mod = mp_wasm_pack_bind(fqn, h, p, len);
        mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
            MP_OBJ_NEW_QSTR(MP_QSTR___wasm_elf__),
            mp_obj_new_int_from_ull((uint64_t)(uintptr_t)img));
        MICROPY_WASM_FREE(owned);
        return mod;
    }
#else
    (void)kind;
#endif

    h = pm_wasmmod_loader_load((const uint8_t *)fqn, (uint32_t)flen, p, len);
    if (h.index == UINT32_MAX) {
        MICROPY_WASM_FREE(owned);
        mp_raise_OSError(MP_EINVAL);
    }
    mod = mp_wasm_pack_bind(fqn, h, p, len);
    MICROPY_WASM_FREE(owned);
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
    size_t flen = strlen(fqn);
    size_t elen = strlen(exp);
    int32_t out = 0;
    int32_t st;

    /* Prefer api scalar helpers (CALLGRAPH) when arity is 0 or 2×i32. */
    if (n_args <= 2) {
        st = pm_wasmmod_api_call0_i32((const uint8_t *)fqn, (uint32_t)flen,
            (const uint8_t *)exp, (uint32_t)elen, &out);
    } else {
        size_t len;
        mp_obj_t *items;
        mp_obj_get_array(args[2], &len, &items);
        if (len == 2) {
            st = pm_wasmmod_api_call2_i32((const uint8_t *)fqn, (uint32_t)flen,
                (const uint8_t *)exp, (uint32_t)elen,
                (int32_t)mp_obj_get_int(items[0]), (int32_t)mp_obj_get_int(items[1]), &out);
        } else {
            pm_wasmmod_registry_value_t argv[8];
            if (len > 8) {
                mp_raise_ValueError(MP_ERROR_TEXT("call: at most 8 args"));
            }
            for (size_t i = 0; i < len; ++i) {
                argv[i] = pm_wasmmod_registry_value_i32((int32_t)mp_obj_get_int(items[i]));
            }
            pm_wasmmod_registry_value_t results[1];
            memset(results, 0, sizeof(results));
            st = pm_wasmmod_registry_call(
                (const uint8_t *)fqn, (uint32_t)flen,
                (const uint8_t *)exp, (uint32_t)elen,
                argv, (uint32_t)len, results, 1);
            if (st >= 0 && results[0].kind == PM_WASMMOD_REGISTRY_VALKIND_I32) {
                out = results[0].of.i32;
            } else if (st >= 0) {
                out = st;
            }
        }
    }
    if (st < 0) {
        mp_raise_msg(&mp_type_LookupError, MP_ERROR_TEXT("registry call miss"));
    }
    return mp_obj_new_int(out);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_call_obj, 2, 3, mod_wasm_call);

static mp_obj_t mod_wasm_connect(mp_obj_t fqn_in, mp_obj_t exp_in) {
    pm_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    const char *exp = mp_obj_str_get_str(exp_in);
    pm_wasmmod_registry_fn_t fn = NULL;
    int32_t st = pm_wasmmod_api_connect((const uint8_t *)fqn, (uint32_t)strlen(fqn),
        (const uint8_t *)exp, (uint32_t)strlen(exp), &fn);
    return mp_obj_new_bool(st == 0 && fn != NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_connect_obj, mod_wasm_connect);

static mp_obj_t mod_wasm_verify(size_t n_args, const mp_obj_t *args) {
    pm_wasm_ensure_inited();
    if (n_args == 1 && (args[0] == mp_const_true || args[0] == mp_const_false)) {
        mp_wasm_set_verify_enabled(args[0] == mp_const_true);
        return mp_obj_new_bool(mp_wasm_get_verify_enabled());
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    char err[160];
    bool ok = mp_wasm_verify_sig((const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len, err,
        sizeof(err));
    if (!ok) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("verify: %s"), err);
    }
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_verify_obj, 1, 1, mod_wasm_verify);

static int source_list_cb(void *ctx, const char *path, size_t path_len) {
    mp_obj_t list = *(mp_obj_t *)ctx;
    mp_obj_list_append(list, mp_obj_new_str(path, path_len));
    return 0;
}

static mp_obj_t mod_wasm_source_list(mp_obj_t name_in) {
    pm_wasm_ensure_inited();
    const char *name = mp_obj_str_get_str(name_in);
    vstr_t path;
    if (!mp_wasm_find_pack(name, &path)) {
        mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("no pack for '%s'"), name);
    }
    uint8_t *bytes = NULL;
    size_t blen = 0;
    if (!mp_wasm_read_file(vstr_null_terminated_str(&path), &bytes, &blen)) {
        vstr_clear(&path);
        mp_raise_OSError(MP_ENOENT);
    }
    vstr_clear(&path);
    const uint8_t *p = bytes;
    uint32_t len = (uint32_t)blen;
    uint8_t *owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&p, &len, &owned)) {
        m_del(uint8_t, bytes, blen);
        mp_raise_ValueError(MP_ERROR_TEXT("corrupt MPZL artifact"));
    }
    mp_wasm_source_view_t *v = mp_wasm_source_open_buffer(p, len);
    mp_obj_t out = mp_obj_new_list(0, NULL);
    if (v != NULL) {
        (void)mp_wasm_source_list_files(v, NULL, &out, source_list_cb);
        mp_wasm_source_close(v);
    }
    MICROPY_WASM_FREE(owned);
    m_del(uint8_t, bytes, blen);
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_source_list_obj, mod_wasm_source_list);

#if MICROPY_PY_WASM_GEN

/* ---- live µPy → rich __init__.pyi ---- */

static vstr_t gen_pyi_buf;
static int gen_pyi_buf_inited;

static int32_t gen_py_face_provider(void *ctx, const uint8_t *fqn, uint32_t fqn_len,
    uint8_t *buf, uint32_t *inout_len) {
    (void)ctx;
    if (fqn == NULL || fqn_len == 0 || inout_len == NULL) {
        return -1;
    }
    if (!gen_pyi_buf_inited) {
        vstr_init(&gen_pyi_buf, 256);
        gen_pyi_buf_inited = 1;
    }
    if (buf == NULL) {
        vstr_reset(&gen_pyi_buf);
        nlr_buf_t nlr;
        if (nlr_push(&nlr) != 0) {
            return 0;
        }
        qstr q = qstr_from_strn((const char *)fqn, fqn_len);
        mp_obj_t mod = mp_import_name(q, mp_const_empty_tuple, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_dict_t *globals = mp_obj_module_get_globals(mod);
        if (globals == NULL) {
            nlr_pop();
            return 0;
        }
        vstr_add_str(&gen_pyi_buf, "# DO NOT EDIT — generated by `pymergetic.util.gen` (live µPy import).\n# ");
        vstr_add_strn(&gen_pyi_buf, (const char *)fqn, fqn_len);
        vstr_add_str(&gen_pyi_buf, "\n\nfrom typing import Any\n\n");
        int any = 0;
        mp_map_t *map = &globals->map;
        for (size_t i = 0; i < map->alloc; ++i) {
            if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_qstr(map->table[i].key)) {
                continue;
            }
            const char *name = qstr_str(MP_OBJ_QSTR_VALUE(map->table[i].key));
            if (name[0] == '_') {
                continue;
            }
            if (!mp_obj_is_callable(map->table[i].value)) {
                continue;
            }
            vstr_add_str(&gen_pyi_buf, "def ");
            vstr_add_str(&gen_pyi_buf, name);
            vstr_add_str(&gen_pyi_buf, "(*args: Any, **kwargs: Any) -> Any: ...\n\n");
            any = 1;
        }
        nlr_pop();
        if (!any) {
            return 0;
        }
        *inout_len = (uint32_t)gen_pyi_buf.len;
        return 1;
    }
    uint32_t n = *inout_len;
    if (n > gen_pyi_buf.len) {
        n = (uint32_t)gen_pyi_buf.len;
    }
    memcpy(buf, gen_pyi_buf.buf, n);
    *inout_len = n;
    return 1;
}

/* ---- µPy VFS → pm_util_gen_run_vfs ops ---- */

static int32_t gen_vfs_read(void *ctx, const uint8_t *path, uint32_t path_len,
    uint8_t *buf, uint32_t *inout_len) {
    (void)ctx;
    if (path == NULL || path_len == 0 || inout_len == NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return 0; /* missing / error → treat as absent for check */
    }
    mp_obj_t path_obj = mp_obj_new_str((const char *)path, path_len);
    mp_obj_t mode = mp_obj_new_str("rb", 2);
    mp_obj_t open_args[2] = { path_obj, mode };
    mp_obj_t f = mp_vfs_open(2, open_args, (mp_map_t *)&mp_const_empty_map);
    const mp_stream_p_t *stream = mp_get_stream_raise(f, MP_STREAM_OP_READ);
    vstr_t data;
    vstr_init(&data, 256);
    uint8_t chunk[512];
    for (;;) {
        int err = 0;
        mp_uint_t n = stream->read(f, chunk, sizeof(chunk), &err);
        if (n == MP_STREAM_ERROR) {
            vstr_clear(&data);
            mp_stream_close(f);
            nlr_pop();
            return -1;
        }
        if (n == 0) {
            break;
        }
        vstr_add_strn(&data, (const char *)chunk, n);
    }
    mp_stream_close(f);
    nlr_pop();
    if (buf == NULL) {
        *inout_len = (uint32_t)data.len;
        vstr_clear(&data);
        return 1;
    }
    uint32_t n = *inout_len;
    if (n > data.len) {
        n = (uint32_t)data.len;
    }
    memcpy(buf, data.buf, n);
    *inout_len = n;
    vstr_clear(&data);
    return 1;
}

static int32_t gen_vfs_write(void *ctx, const uint8_t *path, uint32_t path_len,
    const uint8_t *data, uint32_t data_len) {
    (void)ctx;
    if (path == NULL || path_len == 0) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        return -1;
    }
    mp_obj_t path_obj = mp_obj_new_str((const char *)path, path_len);
    mp_obj_t mode = mp_obj_new_str("wb", 2);
    mp_obj_t open_args[2] = { path_obj, mode };
    mp_obj_t f = mp_vfs_open(2, open_args, (mp_map_t *)&mp_const_empty_map);
    const mp_stream_p_t *stream = mp_get_stream_raise(f, MP_STREAM_OP_WRITE);
    int err = 0;
    mp_uint_t n = stream->write(f, data, data_len, &err);
    mp_stream_close(f);
    nlr_pop();
    if (n == MP_STREAM_ERROR || n != data_len) {
        return -1;
    }
    return 0;
}

static void gen_install_py_face(void) {
    pm_util_gen_set_py_face_provider(gen_py_face_provider, NULL);
}

static mp_obj_t mod_wasm_gen(size_t n_args, const mp_obj_t *args) {
    /* pymergetic.wasmmod.gen(root, check=False) — live registry facegen → FS. */
    pm_wasm_ensure_inited();
    gen_install_py_face();
    const char *root = ".";
    int check = 0;
    if (n_args >= 1 && args[0] != mp_const_none) {
        root = mp_obj_str_get_str(args[0]);
    }
    if (n_args >= 2) {
        check = mp_obj_is_true(args[1]);
    }
    int32_t rc = pm_util_gen_run((const uint8_t *)root, (uint32_t)strlen(root), check);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_gen_obj, 0, 2, mod_wasm_gen);

static mp_obj_t mod_util_gen_run(size_t n_args, const mp_obj_t *args) {
    return mod_wasm_gen(n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_util_gen_run_obj, 0, 2, mod_util_gen_run);

static mp_obj_t mod_util_gen_run_vfs(size_t n_args, const mp_obj_t *args) {
    /* run_vfs(dir, fqn, check=False) — emit one module into µPy VFS. */
    pm_wasm_ensure_inited();
    gen_install_py_face();
    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("run_vfs needs dir and fqn"));
    }
    const char *dir = mp_obj_str_get_str(args[0]);
    const char *fqn = mp_obj_str_get_str(args[1]);
    int check = 0;
    if (n_args >= 3) {
        check = mp_obj_is_true(args[2]);
    }
    pm_util_gen_vfs_ops_t ops = { .read = gen_vfs_read, .write = gen_vfs_write };
    int32_t rc = pm_util_gen_run_vfs(
        (const uint8_t *)dir, (uint32_t)strlen(dir),
        (const uint8_t *)fqn, (uint32_t)strlen(fqn),
        check, ops, NULL);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_util_gen_run_vfs_obj, 2, 3, mod_util_gen_run_vfs);

static mp_obj_t mod_util_gen_diff(size_t n_args, const mp_obj_t *args) {
    /* diff(fqn, h=None, rs=None, pyi=None) — compare live faces to included bytes. */
    pm_wasm_ensure_inited();
    gen_install_py_face();
    if (n_args < 1) {
        mp_raise_TypeError(MP_ERROR_TEXT("diff needs fqn"));
    }
    const char *fqn = mp_obj_str_get_str(args[0]);
    const uint8_t *h = NULL, *rs = NULL, *pyi = NULL;
    uint32_t h_len = 0, rs_len = 0, pyi_len = 0;
    size_t l;
    if (n_args >= 2 && args[1] != mp_const_none) {
        h = (const uint8_t *)mp_obj_str_get_data(args[1], &l);
        h_len = (uint32_t)l;
    }
    if (n_args >= 3 && args[2] != mp_const_none) {
        rs = (const uint8_t *)mp_obj_str_get_data(args[2], &l);
        rs_len = (uint32_t)l;
    }
    if (n_args >= 4 && args[3] != mp_const_none) {
        pyi = (const uint8_t *)mp_obj_str_get_data(args[3], &l);
        pyi_len = (uint32_t)l;
    }
    int32_t rc = pm_util_gen_diff_included(
        (const uint8_t *)fqn, (uint32_t)strlen(fqn),
        h, h_len, rs, rs_len, pyi, pyi_len);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_util_gen_diff_obj, 1, 4, mod_util_gen_diff);

static const mp_rom_map_elem_t mp_module_pymergetic_util_gen_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_util_dot_gen) },
    { MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&mod_util_gen_run_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_vfs), MP_ROM_PTR(&mod_util_gen_run_vfs_obj) },
    { MP_ROM_QSTR(MP_QSTR_diff), MP_ROM_PTR(&mod_util_gen_diff_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_util_gen_globals, mp_module_pymergetic_util_gen_globals_table);

const mp_obj_module_t mp_module_pymergetic_util_gen = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_util_gen_globals,
};
#endif /* MICROPY_PY_WASM_GEN */

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
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&mod_wasm_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_verify), MP_ROM_PTR(&mod_wasm_verify_obj) },
    { MP_ROM_QSTR(MP_QSTR_source_list), MP_ROM_PTR(&mod_wasm_source_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_path), MP_ROM_PTR(&mod_wasm_path_obj_fun) },
    { MP_ROM_QSTR(MP_QSTR_path_append), MP_ROM_PTR(&mod_wasm_path_append_obj) },
    { MP_ROM_QSTR(MP_QSTR_install_hook), MP_ROM_PTR(&mod_wasm_install_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_uninstall_hook), MP_ROM_PTR(&mod_wasm_uninstall_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish_presence), MP_ROM_PTR(&mod_wasm_publish_presence_obj) },
    { MP_ROM_QSTR(MP_QSTR_test), MP_ROM_PTR(&mod_wasm_test_obj) },
    { MP_ROM_QSTR(MP_QSTR_test_all), MP_ROM_PTR(&mod_wasm_test_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_tests), MP_ROM_PTR(&mod_wasm_tests_obj) },
    { MP_ROM_QSTR(MP_QSTR_test_count), MP_ROM_PTR(&mod_wasm_test_count_obj) },
#if MICROPY_PY_WASM_GEN
    { MP_ROM_QSTR(MP_QSTR_gen), MP_ROM_PTR(&mod_wasm_gen_obj) },
#endif
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
#if MICROPY_PY_WASM_GEN
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_util_dot_gen, mp_module_pymergetic_util_gen);
#endif
