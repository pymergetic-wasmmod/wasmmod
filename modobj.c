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

#include "extmod/wasmmod/alloc.h"

#include "py/mperrno.h"
#include "py/runtime.h"

#if MICROPY_PY_WASM

#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/mod.h"

static const mp_obj_type_t mp_type_pack_func;

static void pack_module_ensure_open(mp_obj_pack_module_t *self) {
    if (self->mod == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("pack module closed"));
    }
}

static bool py_to_wasm_val(mp_obj_t o, wasm_valkind_t kind, wasm_val_t *out) {
    out->kind = kind;
    switch (kind) {
        case WASM_I32:
            out->of.i32 = (int32_t)mp_obj_get_int(o);
            return true;
        case WASM_I64:
#if MICROPY_LONGINT_IMPL != MICROPY_LONGINT_IMPL_NONE
            out->of.i64 = mp_obj_get_ll(o);
            return true;
#else
            (void)o;
            return false;
#endif
        case WASM_F32:
#if MICROPY_FLOAT_IMPL != MICROPY_FLOAT_IMPL_NONE
            out->of.f32 = mp_obj_get_float_to_f(o);
            return true;
#else
            (void)o;
            return false;
#endif
        case WASM_F64:
#if MICROPY_FLOAT_IMPL != MICROPY_FLOAT_IMPL_NONE
            out->of.f64 = mp_obj_get_float_to_d(o);
            return true;
#else
            (void)o;
            return false;
#endif
        default:
            return false;
    }
}

static mp_obj_t wasm_val_to_py(const wasm_val_t *v) {
    switch (v->kind) {
        case WASM_I32:
            return mp_obj_new_int(v->of.i32);
        case WASM_I64:
#if MICROPY_LONGINT_IMPL != MICROPY_LONGINT_IMPL_NONE
            return mp_obj_new_int_from_ll(v->of.i64);
#else
            return mp_obj_new_int((mp_int_t)v->of.i64);
#endif
        case WASM_F32:
#if MICROPY_FLOAT_IMPL != MICROPY_FLOAT_IMPL_NONE
            return mp_obj_new_float((mp_float_t)v->of.f32);
#else
            (void)v;
            return mp_const_none;
#endif
        case WASM_F64:
#if MICROPY_FLOAT_IMPL != MICROPY_FLOAT_IMPL_NONE
            return mp_obj_new_float((mp_float_t)v->of.f64);
#else
            (void)v;
            return mp_const_none;
#endif
        default:
            return mp_const_none;
    }
}

static mp_obj_t call_export_py(mp_pack_t *mod, const char *fname, size_t n_args, const mp_obj_t *args) {
    uint32_t np = 0, nr = 0;
    wasm_valkind_t *pkinds = NULL;
    wasm_valkind_t *rkinds = NULL;
    if (!mp_pack_func_types(mod, fname, &np, &pkinds, &nr, &rkinds)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm export missing or non-numeric"));
    }
    if (n_args != np) {
        MICROPY_WASM_FREE(pkinds);
        MICROPY_WASM_FREE(rkinds);
        mp_raise_msg_varg(&mp_type_TypeError,
            MP_ERROR_TEXT("function takes %d positional arguments but %d were given"),
            (int)np, (int)n_args);
    }
    wasm_val_t *vargs = NULL;
    if (np > 0) {
        vargs = m_new(wasm_val_t, np);
        for (uint32_t i = 0; i < np; ++i) {
            if (!py_to_wasm_val(args[i], pkinds[i], &vargs[i])) {
                MICROPY_WASM_FREE(pkinds);
                MICROPY_WASM_FREE(rkinds);
                mp_raise_TypeError(MP_ERROR_TEXT("unsupported wasm value type"));
            }
        }
    }
    wasm_val_t *results = NULL;
    if (nr > 0) {
        results = m_new(wasm_val_t, nr);
        memset(results, 0, nr * sizeof(*results));
    }
    char err[128];
    bool ok = mp_pack_call_vals(mod, fname, np, vargs, nr, results, err, sizeof(err));
    MICROPY_WASM_FREE(pkinds);
    MICROPY_WASM_FREE(rkinds);
    if (!ok) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm: %s"), err);
    }
    mp_obj_t out;
    if (nr == 0) {
        out = mp_const_none;
    } else if (nr == 1) {
        out = wasm_val_to_py(&results[0]);
    } else {
        mp_obj_t *items = m_new(mp_obj_t, nr);
        for (uint32_t i = 0; i < nr; ++i) {
            items[i] = wasm_val_to_py(&results[i]);
        }
        out = mp_obj_new_tuple(nr, items);
    }
    return out;
}

static mp_obj_t wasm_func_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_obj_pack_func_t *self = MP_OBJ_TO_PTR(self_in);
    if (n_kw) {
        mp_raise_TypeError(MP_ERROR_TEXT("unexpected kwargs"));
    }
    if (self->mod == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm module closed"));
    }
    return call_export_py(self->mod, qstr_str(self->export_name), n_args, args);
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_pack_func,
    MP_QSTR_WasmFunc,
    MP_TYPE_FLAG_NONE,
    call, wasm_func_call
    );

mp_obj_t mp_wasm_func_new(mp_pack_t *mod, qstr export_name) {
    mp_obj_pack_func_t *f = mp_obj_malloc(mp_obj_pack_func_t, (mp_obj_type_t *)&mp_type_pack_func);
    f->mod = mod;
    f->export_name = export_name;
    return MP_OBJ_FROM_PTR(f);
}

mp_obj_t wasm_module_close(mp_obj_t self_in) {
    mp_obj_pack_module_t *self = MP_OBJ_TO_PTR(self_in);
    mp_pack_close(self->mod);
    self->mod = NULL;
    self->pack_name = 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(wasm_module_close_obj, wasm_module_close);

static mp_obj_t wasm_module_call(size_t n_args, const mp_obj_t *args) {
    mp_obj_pack_module_t *self = MP_OBJ_TO_PTR(args[0]);
    pack_module_ensure_open(self);
    const char *fname = mp_obj_str_get_str(args[1]);
    return call_export_py(self->mod, fname, n_args - 2, args + 2);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(wasm_module_call_obj, 2, wasm_module_call);

#if MICROPY_ENABLE_FINALISER
static mp_obj_t wasm_module___del__(mp_obj_t self_in) {
    return wasm_module_close(self_in);
}
static MP_DEFINE_CONST_FUN_OBJ_1(wasm_module___del___obj, wasm_module___del__);
#endif

static mp_obj_t wasm_module_memory_read(mp_obj_t self_in, mp_obj_t off_in, mp_obj_t n_in) {
    mp_obj_pack_module_t *self = MP_OBJ_TO_PTR(self_in);
    pack_module_ensure_open(self);
    uint32_t off = (uint32_t)mp_obj_get_int(off_in);
    mp_int_t n = mp_obj_get_int(n_in);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("memory_read: negative length"));
    }
    vstr_t vstr;
    vstr_init_len(&vstr, (size_t)n);
    if (!mp_pack_mem_read(self->mod, off, (uint32_t)n, vstr.buf)) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("memory_read: bad offset/length"));
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_3(wasm_module_memory_read_obj, wasm_module_memory_read);

static mp_obj_t wasm_module_memory_write(mp_obj_t self_in, mp_obj_t off_in, mp_obj_t data_in) {
    mp_obj_pack_module_t *self = MP_OBJ_TO_PTR(self_in);
    pack_module_ensure_open(self);
    uint32_t off = (uint32_t)mp_obj_get_int(off_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_pack_mem_write(self->mod, off, (uint32_t)bufinfo.len, bufinfo.buf)) {
        mp_raise_ValueError(MP_ERROR_TEXT("memory_write: bad offset/length"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(wasm_module_memory_write_obj, wasm_module_memory_write);

static mp_obj_t wasm_module_memory_alloc(mp_obj_t self_in, mp_obj_t n_in) {
    mp_obj_pack_module_t *self = MP_OBJ_TO_PTR(self_in);
    pack_module_ensure_open(self);
    mp_int_t n = mp_obj_get_int(n_in);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("memory_alloc: negative size"));
    }
    uint32_t off = mp_pack_mem_alloc(self->mod, (uint32_t)n, NULL);
    if (off == 0 && n != 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_obj_new_int_from_uint(off);
}
static MP_DEFINE_CONST_FUN_OBJ_2(wasm_module_memory_alloc_obj, wasm_module_memory_alloc);

static mp_obj_t wasm_module_memory_free(mp_obj_t self_in, mp_obj_t off_in) {
    mp_obj_pack_module_t *self = MP_OBJ_TO_PTR(self_in);
    pack_module_ensure_open(self);
    mp_pack_mem_free(self->mod, (uint32_t)mp_obj_get_int(off_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(wasm_module_memory_free_obj, wasm_module_memory_free);

static const mp_rom_map_elem_t wasm_module_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_call), MP_ROM_PTR(&wasm_module_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&wasm_module_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_memory_read), MP_ROM_PTR(&wasm_module_memory_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_memory_write), MP_ROM_PTR(&wasm_module_memory_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_memory_alloc), MP_ROM_PTR(&wasm_module_memory_alloc_obj) },
    { MP_ROM_QSTR(MP_QSTR_memory_free), MP_ROM_PTR(&wasm_module_memory_free_obj) },
    #if MICROPY_ENABLE_FINALISER
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&wasm_module___del___obj) },
    #endif
};
static MP_DEFINE_CONST_DICT(wasm_module_locals, wasm_module_locals_table);

static void pack_module_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (dest[0] != MP_OBJ_NULL) {
        // read-only attributes
        return;
    }
    mp_obj_pack_module_t *self = MP_OBJ_TO_PTR(self_in);
    pack_module_ensure_open(self);
    const char *s = NULL;
    if (attr == MP_QSTR_kind) {
        s = mp_pack_kind_str(self->mod);
    } else if (attr == MP_QSTR_origin) {
        s = mp_pack_origin(self->mod);
    } else if (attr == MP_QSTR_arch) {
        s = mp_pack_arch(self->mod);
    } else if (attr == MP_QSTR_name) {
        s = (self->pack_name != 0) ? qstr_str(self->pack_name) : mp_pack_name(self->mod);
    } else {
        // Continue to locals_dict (call/close/memory_*).
        dest[1] = MP_OBJ_SENTINEL;
        return;
    }
    dest[0] = mp_obj_new_str(s, strlen(s));
}

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_pack_module,
    MP_QSTR_PackModule,
    MP_TYPE_FLAG_NONE,
    attr, pack_module_attr,
    locals_dict, &wasm_module_locals
    );


mp_obj_t mp_wasm_wrap_loaded(mp_pack_t *mod) {
    #if MICROPY_ENABLE_FINALISER
    mp_obj_pack_module_t *o = mp_obj_malloc_with_finaliser(mp_obj_pack_module_t, (mp_obj_type_t *)&mp_type_pack_module);
    #else
    mp_obj_pack_module_t *o = mp_obj_malloc(mp_obj_pack_module_t, (mp_obj_type_t *)&mp_type_pack_module);
    #endif
    o->mod = mod;
    o->pack_name = 0;
    return MP_OBJ_FROM_PTR(o);
}

static mp_obj_t mod_wasm_load(mp_obj_t data_in) {
    char err[128];
    mp_pack_t *mod;

    if (mp_obj_is_str(data_in)) {
        const char *path = mp_obj_str_get_str(data_in);
        vstr_t code;
        if (!mp_wasm_fetch(path, &code, err, sizeof(err))) {
            mp_raise_OSError_with_filename(MP_ENOENT, path);
        }
        mod = mp_pack_load_ex((const uint8_t *)code.buf, (uint32_t)code.len,
            NULL, 0, path, path, err, sizeof(err));
        vstr_clear(&code);
    } else {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
        mod = mp_pack_load_ex(bufinfo.buf, (uint32_t)bufinfo.len,
            NULL, 0, NULL, NULL, err, sizeof(err));
    }
    if (mod == NULL) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm load: %s"), err);
    }
    return mp_wasm_wrap_loaded(mod);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_load_obj, mod_wasm_load);

void mp_wasm_fixup_modobj_fun_objs(void)
{
	mp_obj_fun_builtin_fixed_t *f1 =
		(mp_obj_fun_builtin_fixed_t *)&mod_wasm_load_obj;
	f1->base.type = &mp_type_fun_builtin_1;
	f1->fun._1 = mod_wasm_load;
}

#endif // MICROPY_PY_WASM
