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


#include <string.h>

#include "py/mpstate.h"
#include "py/objstr.h"

#if MICROPY_PY_WASM

#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/io.h"
#include "extmod/wasmmod/mod.h"
#include "extmod/wasmmod/verify.h"
#include "extmod/wasmmod/version.h"

#ifndef MICROPY_WASM_PACK_ARCH
#define MICROPY_WASM_PACK_ARCH ""
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

static const mp_rom_map_elem_t mp_module_wasm_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_wasm) },
    #if MICROPY_MODULE_BUILTIN_INIT
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_wasm___init___obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mp_wasm_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_path), MP_ROM_PTR(&MP_STATE_VM(mp_wasm_path_obj)) },
    { MP_ROM_QSTR(MP_QSTR_arch), MP_ROM_PTR(&MP_STATE_VM(mp_wasm_arch_obj)) },
    { MP_ROM_QSTR(MP_QSTR_VERIFY), MP_ROM_INT(MICROPY_WASM_VERIFY) },
    { MP_ROM_QSTR(MP_QSTR_verify), MP_ROM_PTR(&mod_wasm_verify_obj) },
    { MP_ROM_QSTR(MP_QSTR_AOT), MP_ROM_INT(MICROPY_PY_WASM_AOT) },
    { MP_ROM_QSTR(MP_QSTR_JIT), MP_ROM_INT(MICROPY_PY_WASM_JIT) },
    { MP_ROM_QSTR(MP_QSTR_FAST_JIT), MP_ROM_INT(MICROPY_PY_WASM_FAST_JIT) },
    { MP_ROM_QSTR(MP_QSTR_MODE), MP_ROM_INT(MICROPY_WASM_MODE_DEFAULT) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&mod_wasm_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_pack), MP_ROM_PTR(&mod_wasm_load_pack_obj) },
    { MP_ROM_QSTR(MP_QSTR_import_wasm), MP_ROM_PTR(&mod_wasm_import_wasm_obj) },
    { MP_ROM_QSTR(MP_QSTR_install_hook), MP_ROM_PTR(&mod_wasm_install_hook_obj) },
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
    { MP_ROM_QSTR(MP_QSTR_WasmModule), MP_ROM_PTR(&mp_type_wasm_module) },
};

static MP_DEFINE_CONST_DICT(mp_module_wasm_globals, mp_module_wasm_globals_table);

const mp_obj_module_t mp_module_wasm = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_wasm_globals,
};

MP_REGISTER_MODULE(MP_QSTR_wasm, mp_module_wasm);

#endif // MICROPY_PY_WASM
