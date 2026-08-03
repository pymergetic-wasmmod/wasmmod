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


#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_MOD_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_MOD_H

#include "py/obj.h"
#include "extmod/wasmmod/runtime.h"

#ifndef MICROPY_PY_WASM_AOT
#define MICROPY_PY_WASM_AOT (0)
#endif
#ifndef MICROPY_PY_WASM_JIT
#define MICROPY_PY_WASM_JIT (0)
#endif
#ifndef MICROPY_PY_WASM_FAST_JIT
#define MICROPY_PY_WASM_FAST_JIT (0)
#endif
#ifndef MICROPY_PY_WASM_MATRIX
#define MICROPY_PY_WASM_MATRIX (0)
#endif

#if MICROPY_PY_WASM_JIT && MICROPY_PY_WASM_FAST_JIT
#define MICROPY_WASM_MODE_DEFAULT ((mp_int_t)Mode_Multi_Tier_JIT)
#elif MICROPY_PY_WASM_JIT
#define MICROPY_WASM_MODE_DEFAULT ((mp_int_t)Mode_LLVM_JIT)
#elif MICROPY_PY_WASM_FAST_JIT
#define MICROPY_WASM_MODE_DEFAULT ((mp_int_t)Mode_Fast_JIT)
#else
#define MICROPY_WASM_MODE_DEFAULT ((mp_int_t)Mode_Interp)
#endif

typedef struct _mp_obj_wasm_module_t {
    mp_obj_base_t base;
    mp_wasm_module_t *mod;
    qstr pack_name; // 0 if not published as a pack
} mp_obj_wasm_module_t;

typedef struct _mp_obj_wasm_func_t {
    mp_obj_base_t base;
    mp_wasm_module_t *mod;
    qstr export_name;
} mp_obj_wasm_func_t;

extern const mp_obj_type_t mp_type_wasm_module;

void mp_wasm_path_ensure(void);
mp_obj_t mp_wasm_path_obj(void);
void mp_wasm_arch_ensure(void);
mp_obj_t mp_wasm_arch_obj(void);

mp_obj_t mp_wasm_wrap_loaded(mp_wasm_module_t *mod);
mp_obj_t mp_wasm_func_new(mp_wasm_module_t *mod, qstr export_name);
mp_obj_t wasm_module_close(mp_obj_t self_in);

mp_obj_t mp_wasm_load_pack_path(const char *path, const char *name_override);

extern int mp_wasm_import_hook_depth;

// Python API fun objs (module globals)
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_load_obj);
MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_load_pack_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_unload_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_import_wasm_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(mod_wasm_install_hook_obj);
MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm_uninstall_hook_obj);
MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_verify_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_sig_info_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_verify_sig_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_add_trust_obj);
MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm_trust_clear_obj);
MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm_trust_count_obj);
MP_DECLARE_CONST_FUN_OBJ_2(mod_wasm_host_set_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_host_get_obj);
MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_host_clear_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_mem_alloc_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_mem_free_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_mem_get_obj);
MP_DECLARE_CONST_FUN_OBJ_2(mod_wasm_mem_set_obj);
MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm_mem_clear_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_handle_register_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_handle_resolve_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_handle_free_obj);
MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm_handle_clear_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_source_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_source_from_file_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_source_from_bytes_obj);
#if MICROPY_PY_WASM_MATRIX
MP_DECLARE_CONST_FUN_OBJ_VAR(mod_wasm_c_call_obj);
MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_c_call_attr_obj);
MP_DECLARE_CONST_FUN_OBJ_VAR(mod_wasm_rs_call_obj);
MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_rs_call_attr_obj);
#endif
#if MICROPY_MODULE_BUILTIN_INIT
MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm___init___obj);
#endif

extern const mp_obj_type_t mp_type_wasm_source;

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_MOD_H
