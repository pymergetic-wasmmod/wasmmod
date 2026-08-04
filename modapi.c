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


#include "py/mperrno.h"
#include "py/runtime.h"

#include <stdio.h>
#include <string.h>

#if MICROPY_PY_WASM

#include "extmod/wasmmod/alloc.h"
#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/inspect.h"
#include "extmod/wasmmod/mod.h"
#include "extmod/wasmmod/verify.h"
// wasm.verify() → bool; wasm.verify(False) → session gate for all loads (VFS+HTTP).
static mp_obj_t mod_wasm_verify(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return mp_obj_new_bool(mp_wasm_get_verify_enabled());
    }
    mp_wasm_set_verify_enabled(mp_obj_is_true(args[0]));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_verify_obj, 0, 1, mod_wasm_verify);

static mp_obj_t mod_wasm_sig_info(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    mp_obj_t dict = mp_obj_new_dict(4);
    // Intern keys at runtime so clangd/stale genhdr cannot break on MP_QSTR_*.
    const mp_obj_t k_has = MP_OBJ_NEW_QSTR(qstr_from_strn("has", 3));
    const mp_obj_t k_mpws = MP_OBJ_NEW_QSTR(qstr_from_strn("mpws", 4));
    const mp_obj_t k_sig_len = MP_OBJ_NEW_QSTR(qstr_from_strn("sig_len", 7));
    const mp_obj_t k_chain_len = MP_OBJ_NEW_QSTR(qstr_from_strn("chain_len", 9));
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (!mp_wasm_sig_find(bufinfo.buf, (uint32_t)bufinfo.len, &payload, &payload_len)) {
        mp_obj_dict_store(dict, k_has, mp_const_false);
        return dict;
    }
    mp_wasm_sig_info_t info;
    if (!mp_wasm_sig_parse(payload, payload_len, &info)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.sig_info: bad wasmmod.sig"));
    }
    mp_obj_dict_store(dict, k_has, mp_const_true);
    mp_obj_dict_store(dict, k_mpws, mp_obj_new_bool(info.is_mpws));
    mp_obj_dict_store(dict, k_sig_len, mp_obj_new_int_from_uint(info.sig_len));
    mp_obj_dict_store(dict, k_chain_len, mp_obj_new_int_from_uint(info.chain_len));
    return dict;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_sig_info_obj, mod_wasm_sig_info);

// wasm.verify_sig(bytes) → True / raises ValueError (ignores session gate).
static mp_obj_t mod_wasm_verify_sig(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    char err[96];
    err[0] = '\0';
    if (!mp_wasm_verify_sig(bufinfo.buf, (uint32_t)bufinfo.len, err, sizeof(err))) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("%s"), err[0] != '\0' ? err : "wasm.verify_sig: failed");
    }
    return mp_const_true;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_verify_sig_obj, mod_wasm_verify_sig);

static mp_obj_t mod_wasm_add_trust(mp_obj_t key_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(key_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_wasm_trust_add(bufinfo.buf, bufinfo.len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.add_trust: bad key or trust full"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_add_trust_obj, mod_wasm_add_trust);

static mp_obj_t mod_wasm_trust_clear(void) {
    mp_wasm_trust_clear();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_trust_clear_obj, mod_wasm_trust_clear);

static mp_obj_t mod_wasm_trust_count(void) {
    return MP_OBJ_NEW_SMALL_INT(mp_wasm_trust_count());
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_trust_count_obj, mod_wasm_trust_count);

static mp_obj_t mod_wasm_host_set(mp_obj_t slot_in, mp_obj_t callable) {
    int32_t slot = (int32_t)mp_obj_get_int(slot_in);
    if (!mp_wasm_host_set_slot(slot, callable)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.host_set: bad slot or callable"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_host_set_obj, mod_wasm_host_set);

static mp_obj_t mod_wasm_host_get(mp_obj_t slot_in) {
    return mp_wasm_host_get_slot((int32_t)mp_obj_get_int(slot_in));
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_host_get_obj, mod_wasm_host_get);

static mp_obj_t mod_wasm_host_clear(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        mp_wasm_host_clear_all();
        return mp_const_none;
    }
    if (!mp_wasm_host_set_slot((int32_t)mp_obj_get_int(args[0]), mp_const_none)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.host_clear: bad slot"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_host_clear_obj, 0, 1, mod_wasm_host_clear);

// mem_alloc(n: int) or mem_alloc(data: bytes-like)
static mp_obj_t mod_wasm_mem_alloc(mp_obj_t arg) {
    if (mp_obj_is_int(arg)) {
        mp_int_t n = mp_obj_get_int(arg);
        if (n < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("mem_alloc: negative size"));
        }
        int32_t c = mp_wasm_mem_alloc((uint32_t)n);
        if (c == 0 && n != 0) {
            mp_raise_OSError(MP_ENOMEM);
        }
        return mp_obj_new_int(c);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(arg, &bufinfo, MP_BUFFER_READ);
    int32_t c = mp_wasm_mem_alloc_copy(bufinfo.buf, (uint32_t)bufinfo.len);
    if (c == 0 && bufinfo.len != 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_obj_new_int(c);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_alloc_obj, mod_wasm_mem_alloc);

static mp_obj_t mod_wasm_mem_free(mp_obj_t cookie_in) {
    if (!mp_wasm_mem_free((int32_t)mp_obj_get_int(cookie_in))) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_free: bad cookie"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_free_obj, mod_wasm_mem_free);

static mp_obj_t mod_wasm_mem_get(mp_obj_t cookie_in) {
    int32_t cookie = (int32_t)mp_obj_get_int(cookie_in);
    if (!mp_wasm_mem_valid(cookie)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_get: bad cookie"));
    }
    uint32_t n = 0;
    const uint8_t *p = mp_wasm_mem_data(cookie, &n);
    return mp_obj_new_bytes(p, (size_t)n);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_get_obj, mod_wasm_mem_get);

static mp_obj_t mod_wasm_mem_set(mp_obj_t cookie_in, mp_obj_t data_in) {
    int32_t cookie = (int32_t)mp_obj_get_int(cookie_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_wasm_mem_set(cookie, bufinfo.buf, (uint32_t)bufinfo.len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_set: bad cookie or OOM"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_mem_set_obj, mod_wasm_mem_set);

static mp_obj_t mod_wasm_mem_clear(void) {
    mp_wasm_mem_clear_all();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_mem_clear_obj, mod_wasm_mem_clear);

static mp_obj_t mod_wasm_handle_register(mp_obj_t obj) {
    int32_t h = mp_wasm_handle_register(obj);
    if (h <= 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_obj_new_int(h);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_register_obj, mod_wasm_handle_register);

static mp_obj_t mod_wasm_handle_resolve(mp_obj_t handle_in) {
    return mp_wasm_handle_resolve((int32_t)mp_obj_get_int(handle_in));
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_resolve_obj, mod_wasm_handle_resolve);

static mp_obj_t mod_wasm_handle_free(mp_obj_t handle_in) {
    if (!mp_wasm_handle_free((int32_t)mp_obj_get_int(handle_in))) {
        mp_raise_ValueError(MP_ERROR_TEXT("handle_free: bad handle"));
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_free_obj, mod_wasm_handle_free);

static mp_obj_t mod_wasm_handle_clear(void) {
    mp_wasm_handle_clear_all();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_handle_clear_obj, mod_wasm_handle_clear);

#if MICROPY_PY_WASM_MATRIX
static void require_str(mp_obj_t o, const char **data, size_t *len) {
    *data = mp_obj_str_get_data(o, len);
}

// Matrix-only: host C → guest Wasm export (i32).
static mp_obj_t mod_wasm_c_call(size_t n_args, const mp_obj_t *args) {
    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("c_call needs pack, func"));
    }
    const char *pack;
    size_t pack_len;
    const char *func;
    size_t func_len;
    require_str(args[0], &pack, &pack_len);
    require_str(args[1], &func, &func_len);
    uint32_t nargs = (uint32_t)(n_args - 2);
    int32_t stack_args[4];
    int32_t *iargs = NULL;
    if (nargs > 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("c_call: too many args"));
    }
    if (nargs > 0) {
        for (uint32_t i = 0; i < nargs; ++i) {
            stack_args[i] = (int32_t)mp_obj_get_int(args[2 + i]);
        }
        iargs = stack_args;
    }
    int32_t out = 0;
    if (mp_wasm_host_call_export_i32(pack, pack_len, func, func_len, nargs, iargs, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("c_call: export call failed"));
    }
    return mp_obj_new_int(out);
}
MP_DEFINE_CONST_FUN_OBJ_VAR(mod_wasm_c_call_obj, 2, mod_wasm_c_call);

static mp_obj_t mod_wasm_c_call_attr(size_t n_args, const mp_obj_t *args) {
    const char *mod;
    size_t mod_len;
    const char *attr;
    size_t attr_len;
    require_str(args[0], &mod, &mod_len);
    require_str(args[1], &attr, &attr_len);
    int has_arg = n_args > 2;
    int32_t arg = has_arg ? (int32_t)mp_obj_get_int(args[2]) : 0;
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_call_attr(mod, mod_len, attr, attr_len, has_arg, arg, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("c_call_attr: call failed"));
    }
    return out;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_c_call_attr_obj, 2, 3, mod_wasm_c_call_attr);

// examples/wasmmod/host_matrix.rs
int mp_wasm_host_rs_call_export_i32(const char *pack, size_t pack_len,
    const char *func, size_t func_len, uint32_t nargs, const int32_t *args, int32_t *out);
int mp_wasm_host_rs_call_attr(const char *mod, size_t mod_len,
    const char *attr, size_t attr_len, int has_arg, int32_t arg, mp_obj_t *out);

static mp_obj_t mod_wasm_rs_call(size_t n_args, const mp_obj_t *args) {
    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("rs_call needs pack, func"));
    }
    const char *pack;
    size_t pack_len;
    const char *func;
    size_t func_len;
    require_str(args[0], &pack, &pack_len);
    require_str(args[1], &func, &func_len);
    uint32_t nargs = (uint32_t)(n_args - 2);
    int32_t stack_args[4];
    int32_t *iargs = NULL;
    if (nargs > 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("rs_call: too many args"));
    }
    if (nargs > 0) {
        for (uint32_t i = 0; i < nargs; ++i) {
            stack_args[i] = (int32_t)mp_obj_get_int(args[2 + i]);
        }
        iargs = stack_args;
    }
    int32_t out = 0;
    if (mp_wasm_host_rs_call_export_i32(pack, pack_len, func, func_len, nargs, iargs, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("rs_call: export call failed"));
    }
    return mp_obj_new_int(out);
}
MP_DEFINE_CONST_FUN_OBJ_VAR(mod_wasm_rs_call_obj, 2, mod_wasm_rs_call);

static mp_obj_t mod_wasm_rs_call_attr(size_t n_args, const mp_obj_t *args) {
    const char *mod;
    size_t mod_len;
    const char *attr;
    size_t attr_len;
    require_str(args[0], &mod, &mod_len);
    require_str(args[1], &attr, &attr_len);
    int has_arg = n_args > 2;
    int32_t arg = has_arg ? (int32_t)mp_obj_get_int(args[2]) : 0;
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_rs_call_attr(mod, mod_len, attr, attr_len, has_arg, arg, &out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("rs_call_attr: call failed"));
    }
    return out;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_rs_call_attr_obj, 2, 3, mod_wasm_rs_call_attr);
#endif // MICROPY_PY_WASM_MATRIX

// --- wasmmod.source view ---------------------------------------------------

#include "extmod/wasmmod/source.h"

typedef struct _mp_obj_wasm_source_t {
    mp_obj_base_t base;
    mp_wasm_source_view_t *view;
    char module_filter[128]; // empty = pack view; else dotted relative module
    bool borrowed; // true if view owned by another WasmSource
} mp_obj_wasm_source_t;

static mp_obj_t wasm_source_make(mp_wasm_source_view_t *view, const char *module_filter) {
    if (view == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.source: no wasmmod.source section"));
    }
    mp_obj_wasm_source_t *o = mp_obj_malloc(mp_obj_wasm_source_t, &mp_type_wasm_source);
    o->view = view;
    o->borrowed = false;
    o->module_filter[0] = '\0';
    if (module_filter != NULL && module_filter[0] != '\0') {
        size_t n = strlen(module_filter);
        if (n >= sizeof(o->module_filter)) {
            mp_wasm_source_close(view);
            mp_raise_ValueError(MP_ERROR_TEXT("wasm.source: module name too long"));
        }
        memcpy(o->module_filter, module_filter, n + 1);
    }
    return MP_OBJ_FROM_PTR(o);
}

// Load path (str) or buffer into a malloc'd copy; caller frees with MICROPY_WASM_FREE.
static uint8_t *inspect_load_bytes(mp_obj_t path_or_buf, uint32_t *len_out) {
    if (mp_obj_is_str(path_or_buf)) {
        const char *path = mp_obj_str_get_str(path_or_buf);
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            mp_raise_OSError(MP_ENOENT);
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            mp_raise_OSError(MP_EIO);
        }
        long sz = ftell(f);
        if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            mp_raise_OSError(MP_EIO);
        }
        uint8_t *buf = MICROPY_WASM_MALLOC((size_t)sz ? (size_t)sz : 1);
        if (buf == NULL) {
            fclose(f);
            mp_raise_OSError(MP_ENOMEM);
        }
        size_t n = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        if (n != (size_t)sz) {
            MICROPY_WASM_FREE(buf);
            mp_raise_OSError(MP_EIO);
        }
        *len_out = (uint32_t)n;
        return buf;
    }
    mp_buffer_info_t bi;
    mp_get_buffer_raise(path_or_buf, &bi, MP_BUFFER_READ);
    uint8_t *buf = MICROPY_WASM_MALLOC(bi.len ? bi.len : 1);
    if (buf == NULL) {
        mp_raise_OSError(MP_ENOMEM);
    }
    if (bi.len) {
        memcpy(buf, bi.buf, bi.len);
    }
    *len_out = (uint32_t)bi.len;
    return buf;
}

static const char *kind_str(uint8_t k) {
    switch (k) {
        case 1: return "func";
        case 2: return "data";
        case 3: return "export";
        default: return "other";
    }
}

static const char *bind_str(uint8_t b) {
    switch (b) {
        case 0: return "local";
        case 1: return "global";
        case 2: return "weak";
        case 3: return "export";
        default: return "";
    }
}

static const char *role_str(uint8_t r) {
    switch (r) {
        case 0: return "sym";
        case 1: return "dwarf";
        case 2: return "def";
        case 3: return "decl";
        case 4: return "twin";
        default: return "sym";
    }
}

static mp_obj_t mod_wasm_has_dwarf(mp_obj_t path_or_buf) {
    uint32_t len = 0;
    uint8_t *buf = inspect_load_bytes(path_or_buf, &len);
    bool ok = mp_wasm_inspect_has_dwarf(buf, len);
    MICROPY_WASM_FREE(buf);
    return ok ? mp_const_true : mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_has_dwarf_obj, mod_wasm_has_dwarf);

static mp_obj_t mod_wasm_symbols(mp_obj_t path_or_buf) {
    uint32_t len = 0;
    uint8_t *buf = inspect_load_bytes(path_or_buf, &len);
    mp_wasm_sym_t syms[64];
    size_t n = 0;
    bool ok = mp_wasm_inspect_symbols(buf, len, syms, 64, &n);
    MICROPY_WASM_FREE(buf);
    if (!ok) {
        mp_raise_ValueError(MP_ERROR_TEXT("inspect symbols failed"));
    }
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < n; ++i) {
        mp_obj_t d = mp_obj_new_dict(6);
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_name),
            mp_obj_new_str(syms[i].name, strlen(syms[i].name)));
        if (syms[i].section_index >= 0) {
            mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_section_index),
                MP_OBJ_NEW_SMALL_INT(syms[i].section_index));
        } else {
            mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_section_index), mp_const_none);
        }
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_offset),
            mp_obj_new_int_from_ull(syms[i].offset));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_size),
            mp_obj_new_int_from_ull(syms[i].size));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_kind),
            mp_obj_new_str_from_cstr(kind_str(syms[i].kind)));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_binding),
            mp_obj_new_str_from_cstr(bind_str(syms[i].binding)));
        mp_obj_list_append(list, d);
    }
    return list;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_symbols_obj, mod_wasm_symbols);

static mp_obj_t mod_wasm_addr2line(mp_obj_t path_or_buf, mp_obj_t addr_in) {
    uint32_t len = 0;
    uint8_t *buf = inspect_load_bytes(path_or_buf, &len);
    uint64_t addr = mp_obj_get_int_truncated(addr_in);
    mp_wasm_loc_t locs[8];
    size_t n = 0;
    bool ok = mp_wasm_inspect_addr2line(buf, len, addr, locs, 8, &n);
    MICROPY_WASM_FREE(buf);
    if (!ok) {
        mp_raise_ValueError(MP_ERROR_TEXT("inspect addr2line failed"));
    }
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < n; ++i) {
        mp_obj_t d = mp_obj_new_dict(3);
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_path),
            mp_obj_new_str(locs[i].path, strlen(locs[i].path)));
        if (locs[i].line >= 0) {
            mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_line),
                MP_OBJ_NEW_SMALL_INT(locs[i].line));
        } else {
            mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_line), mp_const_none);
        }
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_role),
            mp_obj_new_str_from_cstr(role_str(locs[i].role)));
        mp_obj_list_append(list, d);
    }
    return list;
}
MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_addr2line_obj, mod_wasm_addr2line);

static mp_obj_t mod_wasm_source(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    return wasm_source_make(mp_wasm_source_open_name(name), NULL);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_source_obj, mod_wasm_source);

static mp_obj_t mod_wasm_source_from_file(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    return wasm_source_make(mp_wasm_source_open_file(path), NULL);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_source_from_file_obj, mod_wasm_source_from_file);

static mp_obj_t mod_wasm_source_from_bytes(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    uint8_t *copy = MICROPY_WASM_MALLOC(bufinfo.len ? bufinfo.len : 1);
    if (copy == NULL) {
        mp_raise_OSError(MP_ENOMEM);
    }
    if (bufinfo.len) {
        memcpy(copy, bufinfo.buf, bufinfo.len);
    }
    return wasm_source_make(mp_wasm_source_open_owned(copy, (uint32_t)bufinfo.len), NULL);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_source_from_bytes_obj, mod_wasm_source_from_bytes);

static mp_obj_t source_close(mp_obj_t self_in) {
    mp_obj_wasm_source_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->view != NULL && !self->borrowed) {
        mp_wasm_source_close(self->view);
    }
    self->view = NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(source_close_obj, source_close);

static mp_obj_t source_meta(mp_obj_t self_in) {
    mp_obj_wasm_source_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->view == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("source view closed"));
    }
    const mp_wasm_source_info_t *info = mp_wasm_source_info(self->view);
    mp_obj_t dict = mp_obj_new_dict(4);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_name),
        mp_obj_new_str(info->name, info->name_len));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_version),
        mp_obj_new_str(info->pkg_version, info->pkg_version_len));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_n_files),
        mp_obj_new_int(info->n_files));
    mp_obj_t tags = mp_obj_new_dict(info->n_tags);
    for (uint16_t i = 0; i < info->n_tags; ++i) {
        mp_obj_dict_store(tags,
            mp_obj_new_str(info->tags[i].key, info->tags[i].key_len),
            mp_obj_new_str(info->tags[i].value, info->tags[i].value_len));
    }
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_tags), tags);
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(source_meta_obj, source_meta);

typedef struct {
    mp_obj_t list;
} path_list_ctx_t;

static int path_list_cb(void *ctx, const char *path, size_t path_len) {
    path_list_ctx_t *c = ctx;
    mp_obj_list_append(c->list, mp_obj_new_str(path, path_len));
    return 0;
}

static mp_obj_t source_files(mp_obj_t self_in) {
    mp_obj_wasm_source_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->view == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("source view closed"));
    }
    path_list_ctx_t ctx = { .list = mp_obj_new_list(0, NULL) };
    const char *mod = self->module_filter[0] ? self->module_filter : NULL;
    // Pack view (no filter): all files. Module view: filtered.
    if (mod == NULL) {
        mp_wasm_source_list_files(self->view, NULL, &ctx, path_list_cb);
    } else {
        mp_wasm_source_list_files(self->view, mod, &ctx, path_list_cb);
    }
    return ctx.list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(source_files_obj, source_files);

static int name_list_cb(void *ctx, const char *name, size_t name_len) {
    path_list_ctx_t *c = ctx;
    mp_obj_list_append(c->list, mp_obj_new_str(name, name_len));
    return 0;
}

static mp_obj_t source_modules(mp_obj_t self_in) {
    mp_obj_wasm_source_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->view == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("source view closed"));
    }
    path_list_ctx_t ctx = { .list = mp_obj_new_list(0, NULL) };
    mp_wasm_source_list_modules(self->view, &ctx, name_list_cb);
    return ctx.list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(source_modules_obj, source_modules);

static mp_obj_t source_submodules(mp_obj_t self_in) {
    mp_obj_wasm_source_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->view == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("source view closed"));
    }
    path_list_ctx_t ctx = { .list = mp_obj_new_list(0, NULL) };
    mp_wasm_source_list_submodules(self->view, self->module_filter, &ctx, name_list_cb);
    return ctx.list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(source_submodules_obj, source_submodules);

static mp_obj_t source_module(mp_obj_t self_in, mp_obj_t name_in) {
    mp_obj_wasm_source_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->view == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("source view closed"));
    }
    const char *child = mp_obj_str_get_str(name_in);
    char buf[128];
    if (self->module_filter[0] == '\0') {
        if (strlen(child) >= sizeof(buf)) {
            mp_raise_ValueError(MP_ERROR_TEXT("module name too long"));
        }
        strcpy(buf, child);
    } else {
        if (snprintf(buf, sizeof(buf), "%s.%s", self->module_filter, child) >= (int)sizeof(buf)) {
            mp_raise_ValueError(MP_ERROR_TEXT("module name too long"));
        }
    }
    mp_obj_wasm_source_t *o = mp_obj_malloc(mp_obj_wasm_source_t, &mp_type_wasm_source);
    o->view = self->view;
    o->borrowed = true;
    strcpy(o->module_filter, buf);
    return MP_OBJ_FROM_PTR(o);
}
static MP_DEFINE_CONST_FUN_OBJ_2(source_module_obj, source_module);

static mp_obj_t source_read(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_wasm_source_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->view == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("source view closed"));
    }
    const char *path = mp_obj_str_get_str(path_in);
    uint8_t *data = NULL;
    uint32_t len = 0;
    if (!mp_wasm_source_read(self->view, path, &data, &len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.source.read: path not found"));
    }
    mp_obj_t out = mp_obj_new_bytes(data, len);
    MICROPY_WASM_FREE(data);
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_2(source_read_obj, source_read);

static const mp_rom_map_elem_t source_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&source_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_meta), MP_ROM_PTR(&source_meta_obj) },
    { MP_ROM_QSTR(MP_QSTR_files), MP_ROM_PTR(&source_files_obj) },
    { MP_ROM_QSTR(MP_QSTR_modules), MP_ROM_PTR(&source_modules_obj) },
    { MP_ROM_QSTR(MP_QSTR_submodules), MP_ROM_PTR(&source_submodules_obj) },
    { MP_ROM_QSTR(MP_QSTR_module), MP_ROM_PTR(&source_module_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&source_read_obj) },
};
static MP_DEFINE_CONST_DICT(source_locals, source_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_source,
    MP_QSTR_WasmSource,
    MP_TYPE_FLAG_NONE,
    locals_dict, &source_locals
    );

#endif // MICROPY_PY_WASM
