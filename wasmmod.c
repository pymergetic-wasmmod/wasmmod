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
#include "pm_upy/exec/embed.h"
#include "pm_upy/exec/run.h"
#include "pm_upy/features.h"
#include "pm_upy/hal/time.h"
#include "pm_upy/init.h"
#include "pm_upy/loop/repl.h"
#include "pm_upy/loop/sched.h"
#include "pm_upy/loop/step.h"
#include "pm_upy/obj/call.h"
#include "pm_upy/mem/gc.h"
#include "pm_upy/mem/stack.h"
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
    MP_STATE_VM(mp_wasm_handles) = MP_OBJ_NULL;
    mp_wasm_import_hook_depth = 0;
    mp_wasm_set_verify_enabled(true);
    mp_wasm_io_set(NULL);
    mp_wasm_trust_init_session(); // arm lazy baked-CA load; do not inflate yet
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

static mp_obj_t mod_upy_mem_stack_check(void) {
    pm_upy_stack_check();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_mem_stack_check_obj, mod_upy_mem_stack_check);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_mem_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_mem) },
    { MP_ROM_QSTR(MP_QSTR_gc_collect), MP_ROM_PTR(&mod_upy_mem_gc_collect_obj) },
    { MP_ROM_QSTR(MP_QSTR_gc_enabled), MP_ROM_PTR(&mod_upy_mem_gc_enabled_obj) },
    { MP_ROM_QSTR(MP_QSTR_stack_check), MP_ROM_PTR(&mod_upy_mem_stack_check_obj) },
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

static mp_obj_t mod_upy_time_delay_us(mp_obj_t us_in) {
    mp_int_t us = mp_obj_get_int(us_in);
    if (us < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("delay_us"));
    }
    pm_upy_delay_us((uint32_t)us);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_time_delay_us_obj, mod_upy_time_delay_us);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_time_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_time) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&mod_upy_time_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_us), MP_ROM_PTR(&mod_upy_time_ticks_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_time_ns), MP_ROM_PTR(&mod_upy_time_time_ns_obj) },
    { MP_ROM_QSTR(MP_QSTR_delay_ms), MP_ROM_PTR(&mod_upy_time_delay_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_delay_us), MP_ROM_PTR(&mod_upy_time_delay_us_obj) },
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

static mp_obj_t mod_upy_sched_keyboard_interrupt(void) {
    pm_upy_sched_keyboard_interrupt();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_sched_keyboard_interrupt_obj, mod_upy_sched_keyboard_interrupt);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_sched_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_sched) },
    { MP_ROM_QSTR(MP_QSTR_handle_pending), MP_ROM_PTR(&mod_upy_sched_handle_pending_obj) },
    { MP_ROM_QSTR(MP_QSTR_num_pending), MP_ROM_PTR(&mod_upy_sched_num_pending_obj) },
    { MP_ROM_QSTR(MP_QSTR_event_wait_ms), MP_ROM_PTR(&mod_upy_sched_event_wait_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_lock), MP_ROM_PTR(&mod_upy_sched_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_unlock), MP_ROM_PTR(&mod_upy_sched_unlock_obj) },
    { MP_ROM_QSTR(MP_QSTR_keyboard_interrupt), MP_ROM_PTR(&mod_upy_sched_keyboard_interrupt_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_sched_globals, mp_module_pymergetic_upy_sched_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_sched = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_sched_globals,
};

/* --- pymergetic.upy.call --- */

static mp_obj_t mod_upy_call_fn_resolve(mp_obj_t dotted_in) {
    const char *dotted = mp_obj_str_get_str(dotted_in);
    return mp_obj_new_int_from_uint((mp_uint_t)pm_upy_fn_resolve(dotted));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_call_fn_resolve_obj, mod_upy_call_fn_resolve);

static mp_obj_t mod_upy_call_fn_call_i32(mp_obj_t fn_h_in, mp_obj_t a_in, mp_obj_t b_in) {
    int32_t out = 0;
    int st = pm_upy_fn_call_i32(
        (uint32_t)mp_obj_get_int(fn_h_in),
        (int32_t)mp_obj_get_int(a_in),
        (int32_t)mp_obj_get_int(b_in),
        &out);
    if (st != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("fn_call_i32"));
    }
    return mp_obj_new_int(out);
}
static MP_DEFINE_CONST_FUN_OBJ_3(mod_upy_call_fn_call_i32_obj, mod_upy_call_fn_call_i32);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_call_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_call) },
    { MP_ROM_QSTR(MP_QSTR_fn_resolve), MP_ROM_PTR(&mod_upy_call_fn_resolve_obj) },
    { MP_ROM_QSTR(MP_QSTR_fn_call_i32), MP_ROM_PTR(&mod_upy_call_fn_call_i32_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_call_globals, mp_module_pymergetic_upy_call_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_call = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_call_globals,
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

static mp_obj_t mod_upy_run_run_script(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    if (pm_upy_run_script(path) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("run_script"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_run_run_script_obj, mod_upy_run_run_script);

static mp_obj_t mod_upy_run_parse_compile_execute(mp_obj_t src_in) {
    const char *src = mp_obj_str_get_str(src_in);
    if (pm_upy_parse_compile_execute(src) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("parse_compile_execute"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_run_parse_compile_execute_obj, mod_upy_run_parse_compile_execute);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_run_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_run) },
    { MP_ROM_QSTR(MP_QSTR_run_str), MP_ROM_PTR(&mod_upy_run_run_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_script), MP_ROM_PTR(&mod_upy_run_run_script_obj) },
    { MP_ROM_QSTR(MP_QSTR_parse_compile_execute), MP_ROM_PTR(&mod_upy_run_parse_compile_execute_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_run_globals, mp_module_pymergetic_upy_run_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_run = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_run_globals,
};

/* --- pymergetic.upy.init --- */

static mp_obj_t mod_upy_init_ready(void) {
    return mp_obj_new_bool(pm_upy_ready() != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_init_ready_obj, mod_upy_init_ready);

static mp_obj_t mod_upy_init_deinit(void) {
    pm_upy_deinit();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_init_deinit_obj, mod_upy_init_deinit);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_init_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_init) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&mod_upy_init_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&mod_upy_init_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_init_globals, mp_module_pymergetic_upy_init_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_init = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_init_globals,
};

/* --- pymergetic.upy.step --- */

static mp_obj_t mod_upy_step_loop_step(void) {
    if (pm_upy_loop_step() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("loop_step"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_step_loop_step_obj, mod_upy_step_loop_step);

static mp_obj_t mod_upy_step_loop_reset(void) {
    if (pm_upy_loop_reset() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("loop_reset"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_step_loop_reset_obj, mod_upy_step_loop_reset);

static mp_obj_t mod_upy_step_loop_feed(mp_obj_t data_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    if (pm_upy_loop_feed(bufinfo.buf, bufinfo.len) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("loop_feed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_step_loop_feed_obj, mod_upy_step_loop_feed);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_step_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_step) },
    { MP_ROM_QSTR(MP_QSTR_loop_step), MP_ROM_PTR(&mod_upy_step_loop_step_obj) },
    { MP_ROM_QSTR(MP_QSTR_loop_reset), MP_ROM_PTR(&mod_upy_step_loop_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_loop_feed), MP_ROM_PTR(&mod_upy_step_loop_feed_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_step_globals, mp_module_pymergetic_upy_step_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_step = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_step_globals,
};

/* --- pymergetic.upy.repl --- */

static mp_obj_t mod_upy_repl_start(void) {
    if (pm_upy_repl_start() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("repl_start"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_repl_start_obj, mod_upy_repl_start);

static mp_obj_t mod_upy_repl_stop(void) {
    pm_upy_repl_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_repl_stop_obj, mod_upy_repl_stop);

static mp_obj_t mod_upy_repl_active(void) {
    return mp_obj_new_bool(pm_upy_repl_active() != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_repl_active_obj, mod_upy_repl_active);

static mp_obj_t mod_upy_repl_feed_line(mp_obj_t line_in) {
    const char *line = mp_obj_str_get_str(line_in);
    if (pm_upy_repl_feed_line(line, strlen(line)) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("repl_feed_line"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_repl_feed_line_obj, mod_upy_repl_feed_line);

static mp_obj_t mod_upy_repl_prompt(void) {
    const char *p = pm_upy_repl_prompt();
    return mp_obj_new_str(p, strlen(p ? p : ""));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_repl_prompt_obj, mod_upy_repl_prompt);

static mp_obj_t mod_upy_repl_banner(void) {
    const char *b = pm_upy_repl_banner();
    return mp_obj_new_str(b, strlen(b ? b : ""));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_repl_banner_obj, mod_upy_repl_banner);

static mp_obj_t mod_upy_repl_continue(mp_obj_t src_in) {
    const char *src = mp_obj_str_get_str(src_in);
    return mp_obj_new_bool(pm_upy_repl_continue(src) != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_repl_continue_obj, mod_upy_repl_continue);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_repl_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_repl) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&mod_upy_repl_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&mod_upy_repl_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&mod_upy_repl_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_feed_line), MP_ROM_PTR(&mod_upy_repl_feed_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_prompt), MP_ROM_PTR(&mod_upy_repl_prompt_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner), MP_ROM_PTR(&mod_upy_repl_banner_obj) },
    { MP_ROM_QSTR(MP_QSTR_continue_src), MP_ROM_PTR(&mod_upy_repl_continue_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_repl_globals, mp_module_pymergetic_upy_repl_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_repl = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_repl_globals,
};

/* --- pymergetic.upy.embed --- */

static mp_obj_t mod_upy_embed_exec_str(mp_obj_t src_in) {
    const char *src = mp_obj_str_get_str(src_in);
    if (pm_upy_embed_exec_str(src) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("embed_exec_str"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_upy_embed_exec_str_obj, mod_upy_embed_exec_str);

static mp_obj_t mod_upy_embed_deinit(void) {
    pm_upy_embed_deinit();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_upy_embed_deinit_obj, mod_upy_embed_deinit);

static const mp_rom_map_elem_t mp_module_pymergetic_upy_embed_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy_dot_embed) },
    { MP_ROM_QSTR(MP_QSTR_exec_str), MP_ROM_PTR(&mod_upy_embed_exec_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&mod_upy_embed_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_upy_embed_globals, mp_module_pymergetic_upy_embed_globals_table);

const mp_obj_module_t mp_module_pymergetic_upy_embed = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_upy_embed_globals,
};

static const mp_rom_map_elem_t mp_module_pymergetic_upy_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_upy) },
    { MP_ROM_QSTR(MP_QSTR_mem), MP_ROM_PTR(&mp_module_pymergetic_upy_mem) },
    { MP_ROM_QSTR(MP_QSTR_features), MP_ROM_PTR(&mp_module_pymergetic_upy_features) },
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&mp_module_pymergetic_upy_time) },
    { MP_ROM_QSTR(MP_QSTR_sched), MP_ROM_PTR(&mp_module_pymergetic_upy_sched) },
    { MP_ROM_QSTR(MP_QSTR_call), MP_ROM_PTR(&mp_module_pymergetic_upy_call) },
    { MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&mp_module_pymergetic_upy_run) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&mp_module_pymergetic_upy_init) },
    { MP_ROM_QSTR(MP_QSTR_step), MP_ROM_PTR(&mp_module_pymergetic_upy_step) },
    { MP_ROM_QSTR(MP_QSTR_repl), MP_ROM_PTR(&mp_module_pymergetic_upy_repl) },
    { MP_ROM_QSTR(MP_QSTR_embed), MP_ROM_PTR(&mp_module_pymergetic_upy_embed) },
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
    { MP_ROM_QSTR(MP_QSTR_pack_root), MP_ROM_PTR(&mod_wasm_host_pack_root_obj) },
    { MP_ROM_QSTR(MP_QSTR_pack_files), MP_ROM_PTR(&mod_wasm_host_pack_files_obj) },
    { MP_ROM_QSTR(MP_QSTR_pack_read), MP_ROM_PTR(&mod_wasm_host_pack_read_obj) },
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
    { MP_ROM_QSTR(MP_QSTR_cdn_prepend), MP_ROM_PTR(&mod_wasm_cdn_prepend_obj) },
    { MP_ROM_QSTR(MP_QSTR_catalog), MP_ROM_PTR(&mod_wasm_catalog_obj) },
    { MP_ROM_QSTR(MP_QSTR_session_id), MP_ROM_PTR(&mod_wasm_session_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish), MP_ROM_PTR(&mod_wasm_publish_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish_file), MP_ROM_PTR(&mod_wasm_publish_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_uninstall_hook), MP_ROM_PTR(&mod_wasm_uninstall_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_unload), MP_ROM_PTR(&mod_wasm_unload_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_trust), MP_ROM_PTR(&mod_wasm_add_trust_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_clear), MP_ROM_PTR(&mod_wasm_trust_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_count), MP_ROM_PTR(&mod_wasm_trust_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_export_py), MP_ROM_PTR(&mod_wasm_export_py_obj) },
    { MP_ROM_QSTR(MP_QSTR_export_py_i64), MP_ROM_PTR(&mod_wasm_export_py_i64_obj) },
    { MP_ROM_QSTR(MP_QSTR_export_py_f32), MP_ROM_PTR(&mod_wasm_export_py_f32_obj) },
    { MP_ROM_QSTR(MP_QSTR_export_py_f64), MP_ROM_PTR(&mod_wasm_export_py_f64_obj) },
    { MP_ROM_QSTR(MP_QSTR_export_py_mem), MP_ROM_PTR(&mod_wasm_export_py_mem_obj) },
    { MP_ROM_QSTR(MP_QSTR_export_py_obj), MP_ROM_PTR(&mod_wasm_export_py_handle_obj) },
    { MP_ROM_QSTR(MP_QSTR_export_py_bufptr), MP_ROM_PTR(&mod_wasm_export_py_bufptr_obj) },
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

#if defined(PM_METAL_CFG_FW_BROWSER) && PM_METAL_CFG_FW_BROWSER
/* Metal browser seat: nest path-mirrored metal builtins under this root. */
extern const mp_obj_module_t mp_module_pymergetic_metal;
#endif

static const mp_rom_map_elem_t mp_module_pymergetic_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic) },
    #if MICROPY_MODULE_BUILTIN_INIT
    // Builtin __init__ runs when the registered root is first imported.
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_wasm___init___obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_wasmmod), MP_ROM_PTR(&mp_module_pymergetic_wasmmod) },
    { MP_ROM_QSTR(MP_QSTR_upy), MP_ROM_PTR(&mp_module_pymergetic_upy) },
#if defined(PM_METAL_CFG_FW_BROWSER) && PM_METAL_CFG_FW_BROWSER
    { MP_ROM_QSTR(MP_QSTR_metal), MP_ROM_PTR(&mp_module_pymergetic_metal) },
#endif
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_globals, mp_module_pymergetic_globals_table);

const mp_obj_module_t mp_module_pymergetic = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pymergetic, mp_module_pymergetic);

#endif // MICROPY_PY_WASM
