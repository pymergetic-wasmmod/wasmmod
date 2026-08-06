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


#include <stdio.h>
#include <string.h>

#include "py/mpstate.h"
#include "py/objstr.h"
#include "py/runtime.h"

#if MICROPY_PY_WASM

#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/io.h"
#include "extmod/wasmmod/mod.h"
#include "extmod/wasmmod/verify.h"
#include "extmod/wasmmod/version.h"
#include "pm_upy/exec/await.h"
#include "pm_upy/exec/run.h"
#include "pm_upy/features.h"
#include "pm_upy/hal/time.h"
#include "pm_upy/loop/sched.h"
#include "pm_upy/loop/step.h"
#include "pm_upy/mem/gc.h"
#include "wasm_export.h"

#ifndef MICROPY_WASM_PACK_ARCH
#define MICROPY_WASM_PACK_ARCH ""
#endif

#ifndef MICROPY_WASM_AOT_VERSION
#define MICROPY_WASM_AOT_VERSION (0)
#endif

static const MP_DEFINE_STR_OBJ(mp_wasm_version_obj, MICROPY_WASM_VERSION);

MP_REGISTER_ROOT_POINTER(mp_obj_list_t mp_wasm_path_obj);
MP_REGISTER_ROOT_POINTER(mp_obj_list_t mp_wasm_arch_obj);
MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_prev_import);

void mp_wasm_path_ensure(void) {
    if (MP_STATE_VM(mp_wasm_path_obj).base.type != &mp_type_list) {
        mp_obj_list_init(&MP_STATE_VM(mp_wasm_path_obj), 0);
    }
}

mp_obj_t mp_wasm_path_obj(void) {
    mp_wasm_path_ensure();
    return MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_wasm_path_obj));
}

void mp_wasm_arch_ensure(void) {
    if (MP_STATE_VM(mp_wasm_arch_obj).base.type != &mp_type_list) {
        mp_obj_list_init(&MP_STATE_VM(mp_wasm_arch_obj), 0);
        if (MICROPY_WASM_PACK_ARCH[0] != '\0') {
            mp_obj_list_append(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_wasm_arch_obj)),
                mp_obj_new_str(MICROPY_WASM_PACK_ARCH, strlen(MICROPY_WASM_PACK_ARCH)));
        }
    }
}

mp_obj_t mp_wasm_arch_obj(void) {
    mp_wasm_arch_ensure();
    return MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_wasm_arch_obj));
}

#if MICROPY_MODULE_BUILTIN_INIT
mp_obj_t mod_wasm___init__(void) {
    mp_obj_list_init(&MP_STATE_VM(mp_wasm_path_obj), 0);
    mp_obj_list_init(&MP_STATE_VM(mp_wasm_arch_obj), 0);
    if (MICROPY_WASM_PACK_ARCH[0] != '\0') {
        mp_obj_list_append(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_wasm_arch_obj)),
            mp_obj_new_str(MICROPY_WASM_PACK_ARCH, strlen(MICROPY_WASM_PACK_ARCH)));
    }
    MP_STATE_VM(mp_wasm_prev_import) = MP_OBJ_NULL;
    MP_STATE_VM(mp_wasm_host_slots) = MP_OBJ_NULL;
    MP_STATE_VM(mp_wasm_handles) = MP_OBJ_NULL;
    mp_wasm_import_hook_depth = 0;
    mp_wasm_set_verify_enabled(true);
    mp_wasm_io_set(NULL);
    mp_wasm_trust_init_session(); // arm lazy baked-CA load; do not inflate yet
    mp_wasm_host_clear_all();
    mp_wasm_mem_clear_all();
    mp_wasm_handle_clear_all();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm___init___obj, mod_wasm___init__);
#endif

static mp_obj_t mod_wasm_wamr_version(void) {
    uint32_t major = 0, minor = 0, patch = 0;
    wasm_runtime_get_version(&major, &minor, &patch);
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%u.%u.%u",
        (unsigned)major, (unsigned)minor, (unsigned)patch);
    if (n < 0) {
        n = 0;
    }
    return mp_obj_new_str(buf, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_wamr_version_obj, mod_wasm_wamr_version);

/* --- pymergetic.upy.mem (minimal control-plane face) --- */

static mp_obj_t mod_upy_mem_gc_collect(void) {
    if (pm_upy_gc_collect() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("gc not available"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_mem_gc_collect_obj, mod_upy_mem_gc_collect);

static mp_obj_t mod_upy_mem_gc_enabled(void) {
    return mp_obj_new_bool(pm_upy_gc_enabled() != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_mem_gc_enabled_obj, mod_upy_mem_gc_enabled);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_mem_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_mem) },
    { MP_ROM_QSTR(MP_QSTR_gc_collect), MP_ROM_PTR(&mod_upy_mem_gc_collect_obj) },
    { MP_ROM_QSTR(MP_QSTR_gc_enabled), MP_ROM_PTR(&mod_upy_mem_gc_enabled_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_mem_globals, mp_module_pymergetic_upy_mem_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_mem = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_mem_globals,
};

/* --- pymergetic.upy.features --- */

static mp_obj_t mod_upy_features_bits(void) {
    return mp_obj_new_int_from_uint(pm_upy_features());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_features_bits_obj, mod_upy_features_bits);

static mp_obj_t mod_upy_features_has(mp_obj_t feat_in) {
    return mp_obj_new_bool(pm_upy_has((pm_upy_feat_t)mp_obj_get_int(feat_in)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_features_has_obj, mod_upy_features_has);

static mp_obj_t mod_upy_features_version(void) {
    const char *v = pm_upy_version();
    return mp_obj_new_str(v, strlen(v ? v : ""));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_features_version_obj, mod_upy_features_version);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_features_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_features) },
    { MP_ROM_QSTR(MP_QSTR_bits), MP_ROM_PTR(&mod_upy_features_bits_obj) },
    { MP_ROM_QSTR(MP_QSTR_has), MP_ROM_PTR(&mod_upy_features_has_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mod_upy_features_version_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_features_globals,
    mp_module_pymergetic_upy_features_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_features = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_features_globals,
};

/* --- pymergetic.upy.time --- */

static mp_obj_t mod_upy_time_ticks_ms(void) {
    return mp_obj_new_int_from_uint(pm_upy_ticks_ms());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_time_ticks_ms_obj, mod_upy_time_ticks_ms);

static mp_obj_t mod_upy_time_ticks_us(void) {
    return mp_obj_new_int_from_uint(pm_upy_ticks_us());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_time_ticks_us_obj, mod_upy_time_ticks_us);

static mp_obj_t mod_upy_time_time_ns(void) {
    return mp_obj_new_int_from_ull(pm_upy_time_ns());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_time_time_ns_obj, mod_upy_time_time_ns);

static mp_obj_t mod_upy_time_delay_ms(mp_obj_t ms_in) {
    mp_int_t ms = mp_obj_get_int(ms_in);
    if (ms < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("delay_ms"));
    }
    pm_upy_delay_ms((uint32_t)ms);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_time_delay_ms_obj, mod_upy_time_delay_ms);

static mp_obj_t mod_upy_time_sleep_us(mp_obj_t us_in) {
    uint64_t us = (uint64_t)mp_obj_get_ll(us_in);
    return mp_obj_new_int_from_uint(pm_upy_sleep_us(us));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_time_sleep_us_obj, mod_upy_time_sleep_us);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_time_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_time) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&mod_upy_time_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_us), MP_ROM_PTR(&mod_upy_time_ticks_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_time_ns), MP_ROM_PTR(&mod_upy_time_time_ns_obj) },
    { MP_ROM_QSTR(MP_QSTR_delay_ms), MP_ROM_PTR(&mod_upy_time_delay_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_us), MP_ROM_PTR(&mod_upy_time_sleep_us_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_time_globals, mp_module_pymergetic_upy_time_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_time = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_time_globals,
};

/* --- pymergetic.upy.sched --- */

static mp_obj_t mod_upy_sched_handle_pending(void) {
    if (pm_upy_handle_pending() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("handle_pending"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_sched_handle_pending_obj, mod_upy_sched_handle_pending);

static mp_obj_t mod_upy_sched_num_pending(void) {
    return mp_obj_new_int(pm_upy_sched_num_pending());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_sched_num_pending_obj, mod_upy_sched_num_pending);

static mp_obj_t mod_upy_sched_event_wait_ms(mp_obj_t ms_in) {
    mp_int_t ms = mp_obj_get_int(ms_in);
    if (ms < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("event_wait_ms"));
    }
    if (pm_upy_event_wait_ms((uint32_t)ms) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("event_wait_ms"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_sched_event_wait_ms_obj, mod_upy_sched_event_wait_ms);

static mp_obj_t mod_upy_sched_lock(void) {
    pm_upy_sched_lock();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_sched_lock_obj, mod_upy_sched_lock);

static mp_obj_t mod_upy_sched_unlock(void) {
    pm_upy_sched_unlock();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_sched_unlock_obj, mod_upy_sched_unlock);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_sched_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_sched) },
    { MP_ROM_QSTR(MP_QSTR_handle_pending), MP_ROM_PTR(&mod_upy_sched_handle_pending_obj) },
    { MP_ROM_QSTR(MP_QSTR_num_pending), MP_ROM_PTR(&mod_upy_sched_num_pending_obj) },
    { MP_ROM_QSTR(MP_QSTR_event_wait_ms), MP_ROM_PTR(&mod_upy_sched_event_wait_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_lock), MP_ROM_PTR(&mod_upy_sched_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_unlock), MP_ROM_PTR(&mod_upy_sched_unlock_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_sched_globals, mp_module_pymergetic_upy_sched_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_sched = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_sched_globals,
};

/* --- pymergetic.upy.run --- */

static mp_obj_t mod_upy_run_run_str(mp_obj_t src_in) {
    const char *src = mp_obj_str_get_str(src_in);
    if (pm_upy_run_str(src) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("run_str"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_run_run_str_obj, mod_upy_run_run_str);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_run_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_run) },
    { MP_ROM_QSTR(MP_QSTR_run_str), MP_ROM_PTR(&mod_upy_run_run_str_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_run_globals, mp_module_pymergetic_upy_run_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_run = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_run_globals,
};

static const mp_rom_map_elem_t mp_module_pymergetic_upy_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy) },
    { MP_ROM_QSTR(MP_QSTR_mem), MP_ROM_PTR(&mp_module_pymergetic_upy_mem) },
    { MP_ROM_QSTR(MP_QSTR_features), MP_ROM_PTR(&mp_module_pymergetic_upy_features) },
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&mp_module_pymergetic_upy_time) },
    { MP_ROM_QSTR(MP_QSTR_sched), MP_ROM_PTR(&mp_module_pymergetic_upy_sched) },
    { MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&mp_module_pymergetic_upy_run) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_globals, mp_module_pymergetic_upy_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_globals,
};

/* --- pymergetic.wasmmod.host (self-desc) --- */

static const mp_rom_map_elem_t mp_module_pymergetic_wasmmod_host_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod_dot_host) },
    { MP_ROM_QSTR(MP_QSTR_package_name), MP_ROM_PTR(&mod_wasm_host_package_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_source), MP_ROM_PTR(&mod_wasm_host_source_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_self_image), MP_ROM_PTR(&mod_wasm_host_set_self_image_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_wasmmod_host_globals,
    mp_module_pymergetic_wasmmod_host_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod_host = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_wasmmod_host_globals,
};

/* --- pymergetic.wasmmod (product / pack / inspect face) --- */

static const mp_rom_map_elem_t mp_module_pymergetic_wasmmod_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod) },
    { MP_ROM_QSTR(MP_QSTR_host), MP_ROM_PTR(&mp_module_pymergetic_wasmmod_host) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mp_wasm_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_wamr_version), MP_ROM_PTR(&mod_wasm_wamr_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_path), MP_ROM_PTR(&MP_STATE_VM(mp_wasm_path_obj)) },
    { MP_ROM_QSTR(MP_QSTR_arch), MP_ROM_PTR(&MP_STATE_VM(mp_wasm_arch_obj)) },
    { MP_ROM_QSTR(MP_QSTR_VERIFY), MP_ROM_INT(MICROPY_WASM_VERIFY) },
    { MP_ROM_QSTR(MP_QSTR_verify), MP_ROM_PTR(&mod_wasm_verify_obj) },
    { MP_ROM_QSTR(MP_QSTR_sig_info), MP_ROM_PTR(&mod_wasm_sig_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_verify_sig), MP_ROM_PTR(&mod_wasm_verify_sig_obj) },
    { MP_ROM_QSTR(MP_QSTR_AOT), MP_ROM_INT(MICROPY_PY_WASM_AOT) },
    { MP_ROM_QSTR(MP_QSTR_AOT_VERSION), MP_ROM_INT(MICROPY_WASM_AOT_VERSION) },
    { MP_ROM_QSTR(MP_QSTR_JIT), MP_ROM_INT(MICROPY_PY_WASM_JIT) },
    { MP_ROM_QSTR(MP_QSTR_FAST_JIT), MP_ROM_INT(MICROPY_PY_WASM_FAST_JIT) },
    { MP_ROM_QSTR(MP_QSTR_MODE), MP_ROM_INT(MICROPY_WASM_MODE_DEFAULT) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&mod_wasm_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_pack), MP_ROM_PTR(&mod_wasm_load_pack_obj) },
    { MP_ROM_QSTR(MP_QSTR_import_wasm), MP_ROM_PTR(&mod_wasm_import_wasm_obj) },
    { MP_ROM_QSTR(MP_QSTR_install_hook), MP_ROM_PTR(&mod_wasm_install_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_cdn), MP_ROM_PTR(&mod_wasm_cdn_obj) },
    { MP_ROM_QSTR(MP_QSTR_catalog), MP_ROM_PTR(&mod_wasm_catalog_obj) },
    { MP_ROM_QSTR(MP_QSTR_session_id), MP_ROM_PTR(&mod_wasm_session_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish), MP_ROM_PTR(&mod_wasm_publish_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish_file), MP_ROM_PTR(&mod_wasm_publish_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_uninstall_hook), MP_ROM_PTR(&mod_wasm_uninstall_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_unload), MP_ROM_PTR(&mod_wasm_unload_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_trust), MP_ROM_PTR(&mod_wasm_add_trust_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_clear), MP_ROM_PTR(&mod_wasm_trust_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_count), MP_ROM_PTR(&mod_wasm_trust_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_host_set), MP_ROM_PTR(&mod_wasm_host_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_host_get), MP_ROM_PTR(&mod_wasm_host_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_host_clear), MP_ROM_PTR(&mod_wasm_host_clear_obj) },
    #if MICROPY_PY_WASM_MATRIX
    { MP_ROM_QSTR(MP_QSTR_host_c_triple), MP_ROM_PTR(&mp_wasm_host_c_triple_obj) },
    { MP_ROM_QSTR(MP_QSTR_host_rs_triple), MP_ROM_PTR(&mp_wasm_host_rs_triple_obj) },
    { MP_ROM_QSTR(MP_QSTR_c_call), MP_ROM_PTR(&mod_wasm_c_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_c_call_attr), MP_ROM_PTR(&mod_wasm_c_call_attr_obj) },
    { MP_ROM_QSTR(MP_QSTR_rs_call), MP_ROM_PTR(&mod_wasm_rs_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_rs_call_attr), MP_ROM_PTR(&mod_wasm_rs_call_attr_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_mem_alloc), MP_ROM_PTR(&mod_wasm_mem_alloc_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_free), MP_ROM_PTR(&mod_wasm_mem_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_get), MP_ROM_PTR(&mod_wasm_mem_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_set), MP_ROM_PTR(&mod_wasm_mem_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_clear), MP_ROM_PTR(&mod_wasm_mem_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_handle_register), MP_ROM_PTR(&mod_wasm_handle_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_handle_resolve), MP_ROM_PTR(&mod_wasm_handle_resolve_obj) },
    { MP_ROM_QSTR(MP_QSTR_handle_free), MP_ROM_PTR(&mod_wasm_handle_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_handle_clear), MP_ROM_PTR(&mod_wasm_handle_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_source), MP_ROM_PTR(&mod_wasm_source_obj) },
    { MP_ROM_QSTR(MP_QSTR_source_from_file), MP_ROM_PTR(&mod_wasm_source_from_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_source_from_bytes), MP_ROM_PTR(&mod_wasm_source_from_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_WasmSource), MP_ROM_PTR(&mp_type_wasm_source) },
    { MP_ROM_QSTR(MP_QSTR_has_dwarf), MP_ROM_PTR(&mod_wasm_has_dwarf_obj) },
    { MP_ROM_QSTR(MP_QSTR_symbols), MP_ROM_PTR(&mod_wasm_symbols_obj) },
    { MP_ROM_QSTR(MP_QSTR_addr2line), MP_ROM_PTR(&mod_wasm_addr2line_obj) },
    { MP_ROM_QSTR(MP_QSTR_locations), MP_ROM_PTR(&mod_wasm_locations_obj) },
    { MP_ROM_QSTR(MP_QSTR_disasm), MP_ROM_PTR(&mod_wasm_disasm_obj) },
    { MP_ROM_QSTR(MP_QSTR_mpy_disasm), MP_ROM_PTR(&mod_wasm_mpy_disasm_obj) },
    { MP_ROM_QSTR(MP_QSTR_PackModule), MP_ROM_PTR(&mp_type_pack_module) },
};

static MP_DEFINE_CONST_DICT(mp_module_pymergetic_wasmmod_globals, mp_module_pymergetic_wasmmod_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_wasmmod_globals,
};

/* --- pymergetic (org root; subpackages via MICROPY_MODULE_BUILTIN_SUBPACKAGES) --- */

static const mp_rom_map_elem_t mp_module_pymergetic_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic) },
    #if MICROPY_MODULE_BUILTIN_INIT
    // Builtin __init__ runs when the registered root is first imported.
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_wasm___init___obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_wasmmod), MP_ROM_PTR(&mp_module_pymergetic_wasmmod) },
    { MP_ROM_QSTR(MP_QSTR_upy), MP_ROM_PTR(&mp_module_pymergetic_upy) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_globals, mp_module_pymergetic_globals_table);

const mp_obj_module_t mp_module_pymergetic = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pymergetic, mp_module_pymergetic);

#endif // MICROPY_PY_WASM
