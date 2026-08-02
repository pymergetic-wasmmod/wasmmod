/*
 * This file is part of the MicroPython project, http://micropython.org/
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

#include <stdlib.h>
#include <string.h>

#ifndef MICROPY_WASM_FREE
#define MICROPY_WASM_FREE(p) free(p)
#endif

#include "py/builtin.h"
#include "py/compile.h"
#include "py/emitglue.h"
#include "py/mperrno.h"
#include "py/nlr.h"
#include "py/objmodule.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#include "py/smallint.h"

#if MICROPY_PY_WASM

#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/finder.h"
#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/runtime.h"
#include "extmod/wasmmod/verify.h"

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

// Mirror runtime.c default (WAMR RunningMode).
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

// Bound Wasm export: looks like a normal Python callable (hello(), add(1,2)).
typedef struct _mp_obj_wasm_func_t {
    mp_obj_base_t base;
    mp_wasm_module_t *mod;
    qstr export_name;
} mp_obj_wasm_func_t;

static const mp_obj_type_t mp_type_wasm_module;
static const mp_obj_type_t mp_type_wasm_func;

MP_REGISTER_ROOT_POINTER(mp_obj_list_t mp_wasm_path_obj);
MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_prev_import);

static int wasm_import_hook_depth;

void mp_wasm_path_ensure(void) {
    if (MP_STATE_VM(mp_wasm_path_obj).base.type != &mp_type_list) {
        mp_obj_list_init(&MP_STATE_VM(mp_wasm_path_obj), 0);
    }
}

mp_obj_t mp_wasm_path_obj(void) {
    mp_wasm_path_ensure();
    return MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_wasm_path_obj));
}

static void wasm_module_ensure_open(mp_obj_wasm_module_t *self) {
    if (self->mod == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm module closed"));
    }
}

static bool py_to_wasm_val(mp_obj_t o, wasm_valkind_t kind, wasm_val_t *out) {
    out->kind = kind;
    switch (kind) {
        case WASM_I32:
            out->of.i32 = (int32_t)mp_obj_get_int(o);
            return true;
        case WASM_I64:
            out->of.i64 = mp_obj_get_ll(o);
            return true;
        case WASM_F32:
            out->of.f32 = mp_obj_get_float_to_f(o);
            return true;
        case WASM_F64:
            out->of.f64 = mp_obj_get_float_to_d(o);
            return true;
        default:
            return false;
    }
}

static mp_obj_t wasm_val_to_py(const wasm_val_t *v) {
    switch (v->kind) {
        case WASM_I32:
            return mp_obj_new_int(v->of.i32);
        case WASM_I64:
            return mp_obj_new_int_from_ll(v->of.i64);
        case WASM_F32:
            return mp_obj_new_float((mp_float_t)v->of.f32);
        case WASM_F64:
            return mp_obj_new_float((mp_float_t)v->of.f64);
        default:
            return mp_const_none;
    }
}

static mp_obj_t call_export_py(mp_wasm_module_t *mod, const char *fname, size_t n_args, const mp_obj_t *args) {
    uint32_t np = 0, nr = 0;
    wasm_valkind_t *pkinds = NULL;
    wasm_valkind_t *rkinds = NULL;
    if (!mp_wasm_module_func_types(mod, fname, &np, &pkinds, &nr, &rkinds)) {
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
    bool ok = mp_wasm_module_call_vals(mod, fname, np, vargs, nr, results, err, sizeof(err));
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
    mp_obj_wasm_func_t *self = MP_OBJ_TO_PTR(self_in);
    if (n_kw) {
        mp_raise_TypeError(MP_ERROR_TEXT("unexpected kwargs"));
    }
    if (self->mod == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm module closed"));
    }
    return call_export_py(self->mod, qstr_str(self->export_name), n_args, args);
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_func,
    MP_QSTR_WasmFunc,
    MP_TYPE_FLAG_NONE,
    call, wasm_func_call
    );

static mp_obj_t wasm_func_new(mp_wasm_module_t *mod, qstr export_name) {
    mp_obj_wasm_func_t *f = mp_obj_malloc(mp_obj_wasm_func_t, (mp_obj_type_t *)&mp_type_wasm_func);
    f->mod = mod;
    f->export_name = export_name;
    return MP_OBJ_FROM_PTR(f);
}

static mp_obj_t wasm_module_close(mp_obj_t self_in) {
    mp_obj_wasm_module_t *self = MP_OBJ_TO_PTR(self_in);
    mp_wasm_module_close(self->mod);
    self->mod = NULL;
    self->pack_name = 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(wasm_module_close_obj, wasm_module_close);

static mp_obj_t wasm_module_call(size_t n_args, const mp_obj_t *args) {
    mp_obj_wasm_module_t *self = MP_OBJ_TO_PTR(args[0]);
    wasm_module_ensure_open(self);
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
    mp_obj_wasm_module_t *self = MP_OBJ_TO_PTR(self_in);
    wasm_module_ensure_open(self);
    uint32_t off = (uint32_t)mp_obj_get_int(off_in);
    mp_int_t n = mp_obj_get_int(n_in);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("memory_read: negative length"));
    }
    vstr_t vstr;
    vstr_init_len(&vstr, (size_t)n);
    if (!mp_wasm_module_mem_read(self->mod, off, (uint32_t)n, vstr.buf)) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("memory_read: bad offset/length"));
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_3(wasm_module_memory_read_obj, wasm_module_memory_read);

static mp_obj_t wasm_module_memory_write(mp_obj_t self_in, mp_obj_t off_in, mp_obj_t data_in) {
    mp_obj_wasm_module_t *self = MP_OBJ_TO_PTR(self_in);
    wasm_module_ensure_open(self);
    uint32_t off = (uint32_t)mp_obj_get_int(off_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_wasm_module_mem_write(self->mod, off, (uint32_t)bufinfo.len, bufinfo.buf)) {
        mp_raise_ValueError(MP_ERROR_TEXT("memory_write: bad offset/length"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(wasm_module_memory_write_obj, wasm_module_memory_write);

static mp_obj_t wasm_module_memory_alloc(mp_obj_t self_in, mp_obj_t n_in) {
    mp_obj_wasm_module_t *self = MP_OBJ_TO_PTR(self_in);
    wasm_module_ensure_open(self);
    mp_int_t n = mp_obj_get_int(n_in);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("memory_alloc: negative size"));
    }
    uint32_t off = mp_wasm_module_mem_alloc(self->mod, (uint32_t)n, NULL);
    if (off == 0 && n != 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_obj_new_int_from_uint(off);
}
static MP_DEFINE_CONST_FUN_OBJ_2(wasm_module_memory_alloc_obj, wasm_module_memory_alloc);

static mp_obj_t wasm_module_memory_free(mp_obj_t self_in, mp_obj_t off_in) {
    mp_obj_wasm_module_t *self = MP_OBJ_TO_PTR(self_in);
    wasm_module_ensure_open(self);
    mp_wasm_module_mem_free(self->mod, (uint32_t)mp_obj_get_int(off_in));
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

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_module,
    MP_QSTR_WasmModule,
    MP_TYPE_FLAG_NONE,
    locals_dict, &wasm_module_locals
    );

#if MICROPY_PY_WASM_AOT
static bool ends_with(const char *s, const char *suf) {
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && strcmp(s + n - m, suf) == 0;
}

// Replace suffix in path (e.g. .aot → .wasm). Writes into out.
static bool replace_suffix(const char *path, const char *old_suf, const char *new_suf, vstr_t *out) {
    size_t n = strlen(path), m = strlen(old_suf);
    if (n < m || strcmp(path + n - m, old_suf) != 0) {
        return false;
    }
    vstr_init(out, n - m + strlen(new_suf) + 1);
    vstr_add_strn(out, path, n - m);
    vstr_add_str(out, new_suf);
    return true;
}
#endif

static mp_obj_t wrap_loaded(mp_wasm_module_t *mod) {
    #if MICROPY_ENABLE_FINALISER
    mp_obj_wasm_module_t *o = mp_obj_malloc_with_finaliser(mp_obj_wasm_module_t, (mp_obj_type_t *)&mp_type_wasm_module);
    #else
    mp_obj_wasm_module_t *o = mp_obj_malloc(mp_obj_wasm_module_t, (mp_obj_type_t *)&mp_type_wasm_module);
    #endif
    o->mod = mod;
    o->pack_name = 0;
    return MP_OBJ_FROM_PTR(o);
}

static mp_obj_t mod_wasm_load(mp_obj_t data_in) {
    char err[128];
    mp_wasm_module_t *mod;

    if (mp_obj_is_str(data_in)) {
        const char *path = mp_obj_str_get_str(data_in);
        vstr_t code;
        if (!mp_wasm_fetch(path, &code, err, sizeof(err))) {
            mp_raise_OSError_with_filename(MP_ENOENT, path);
        }
        mod = mp_wasm_module_load_ex((const uint8_t *)code.buf, (uint32_t)code.len,
            NULL, 0, path, path, err, sizeof(err));
        vstr_clear(&code);
    } else {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
        mod = mp_wasm_module_load_ex(bufinfo.buf, (uint32_t)bufinfo.len,
            NULL, 0, NULL, NULL, err, sizeof(err));
    }
    if (mod == NULL) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm load: %s"), err);
    }
    return wrap_loaded(mod);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_load_obj, mod_wasm_load);

typedef struct {
    mp_obj_t py_mod;
    mp_wasm_module_t *wasm;
} bind_ctx_t;

static void bind_export_cb(const char *name, uint32_t nparams, uint32_t nresults, void *ctx_in) {
    (void)nparams;
    (void)nresults;
    bind_ctx_t *ctx = ctx_in;
    if (strcmp(name, "mp_pack_load") == 0 || strcmp(name, "mp_pack_unload") == 0) {
        return;
    }
    mp_obj_t f = wasm_func_new(ctx->wasm, qstr_from_str(name));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(ctx->py_mod)),
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), f);
}

// Logical module path length: strip host tags / extensions.
//   util.py                         → util
//   util.upy.mpy6.sib31.mpy         → util
//   util.cpy.cp312.pyc              → util
//   sub/mod.upy.mpy6.sib63.mpy      → sub/mod
static size_t pack_logical_path_len(const char *path, size_t path_len) {
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (path[i] != '.') {
            continue;
        }
        if (i + 5 <= path_len && memcmp(path + i, ".upy.", 5) == 0) {
            return i;
        }
        if (i + 5 <= path_len && memcmp(path + i, ".cpy.", 5) == 0) {
            return i;
        }
    }
    if (path_len >= 3 && memcmp(path + path_len - 3, ".py", 3) == 0) {
        return path_len - 3;
    }
    if (path_len >= 4 && memcmp(path + path_len - 4, ".mpy", 4) == 0) {
        return path_len - 4;
    }
    if (path_len >= 4 && memcmp(path + path_len - 4, ".pyc", 4) == 0) {
        return path_len - 4;
    }
    return path_len;
}

static bool pack_logical_eq(const char *a, size_t a_len, const char *b, size_t b_len) {
    size_t la = pack_logical_path_len(a, a_len);
    size_t lb = pack_logical_path_len(b, b_len);
    return la == lb && memcmp(a, b, la) == 0;
}

static void path_to_dotted(const char *root, size_t root_len, const char *path, size_t path_len, vstr_t *out) {
    vstr_init(out, root_len + path_len + 4);
    vstr_add_strn(out, root, root_len);
    if (path_len == 0) {
        return;
    }
    size_t n = pack_logical_path_len(path, path_len);
    if (n == 8 && memcmp(path, "__init__", 8) == 0) {
        return;
    }
    if (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0) {
        n -= 9;
    }
    vstr_add_char(out, '.');
    for (size_t i = 0; i < n; ++i) {
        char c = path[i];
        vstr_add_char(out, c == '/' ? '.' : c);
    }
}

// Score a pack file for this MicroPython host. <0 → skip.
// Prefer compatible .mpy (higher sib that still fits), else .py. Ignore .pyc.
static int score_pack_file_for_upy_host(const mp_wasm_pack_file_t *f) {
    const char *path = f->path;
    size_t path_len = f->path_len;

    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".cpy.", 5) == 0) {
            return -1;
        }
    }

    if (f->kind == MP_WASM_PACK_KIND_PYC) {
        return -1;
    }

    if (f->kind == MP_WASM_PACK_KIND_PY) {
        return 1;
    }

    if (f->kind != MP_WASM_PACK_KIND_MPY) {
        return -1;
    }

    #if MICROPY_PERSISTENT_CODE_LOAD
    if (f->data_len < 4 || f->data[0] != 'M' || f->data[1] != MPY_VERSION) {
        return -1;
    }
    // Native arch in feature byte: only accept bytecode (arch == 0) for now.
    if (MPY_FEATURE_DECODE_ARCH(f->data[2]) != MP_NATIVE_ARCH_NONE) {
        return -1;
    }
    if (f->data[3] > MP_SMALL_INT_BITS) {
        return -1;
    }

    // Tagged: ….upy.mpy6.sib31.mpy — prefer highest sib that still fits host.
    const char *tag = NULL;
    size_t tag_len = 0;
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".upy.", 5) == 0) {
            tag = path + i + 5;
            tag_len = path_len - (i + 5);
            break;
        }
    }
    if (tag != NULL && tag_len >= 8 && memcmp(tag, "mpy", 3) == 0) {
        unsigned mpy_ver = 0;
        size_t p = 3;
        while (p < tag_len && tag[p] >= '0' && tag[p] <= '9') {
            mpy_ver = mpy_ver * 10u + (unsigned)(tag[p] - '0');
            ++p;
        }
        const char *sibp = NULL;
        for (size_t j = 0; j + 4 < tag_len; ++j) {
            if (tag[j] == '.' && j + 4 < tag_len && memcmp(tag + j, ".sib", 4) == 0) {
                sibp = tag + j + 4;
                break;
            }
        }
        unsigned sib = 0;
        if (sibp != NULL) {
            while (*sibp >= '0' && *sibp <= '9') {
                sib = sib * 10u + (unsigned)(*sibp - '0');
                ++sibp;
            }
        }
        if (mpy_ver != MPY_VERSION || sib == 0 || sib > (unsigned)MP_SMALL_INT_BITS) {
            return -1;
        }
        return 100 + (int)sib;
    }
    // Legacy untagged .mpy
    return 50 + (int)f->data[3];
    #else
    (void)f;
    return -1;
    #endif
}

static void exec_py_into_module(mp_obj_t module_obj, const char *src_name, const uint8_t *data, uint32_t len) {
    mp_obj_dict_t *globals = mp_obj_module_get_globals(module_obj);
    mp_lexer_t *lex = mp_lexer_new_from_str_len(qstr_from_str(src_name), (const char *)data, len, 0);
    mp_parse_compile_execute(lex, MP_PARSE_FILE_INPUT, globals, globals);
}

#if MICROPY_PERSISTENT_CODE_LOAD
static void exec_mpy_into_module(mp_obj_t module_obj, const char *src_name, const uint8_t *data, uint32_t len) {
    mp_module_context_t *context = (mp_module_context_t *)MP_OBJ_TO_PTR(module_obj);
    mp_compiled_module_t cm;
    cm.context = context;
    mp_raw_code_load_mem(data, len, &cm);

    #if MICROPY_MODULE___FILE__
    mp_store_attr(module_obj, MP_QSTR___file__, MP_OBJ_NEW_QSTR(qstr_from_str(src_name)));
    #else
    (void)src_name;
    #endif

    mp_obj_dict_t *mod_globals = context->module.globals;
    nlr_jump_callback_node_globals_locals_t ctx;
    ctx.globals = mp_globals_get();
    ctx.locals = mp_locals_get();
    mp_globals_set(mod_globals);
    mp_locals_set(mod_globals);
    nlr_push_jump_callback(&ctx.callback, mp_globals_locals_set_from_nlr_jump_callback);
    mp_obj_t module_fun = mp_make_function_from_proto_fun(cm.rc, context, NULL);
    mp_call_function_0(module_fun);
    nlr_pop_jump_callback(true);
}
#endif

static void exec_pack_file_into_module(mp_obj_t module_obj, const char *src_name, const mp_wasm_pack_file_t *f) {
    if (f->kind == MP_WASM_PACK_KIND_PY) {
        exec_py_into_module(module_obj, src_name, f->data, f->data_len);
        return;
    }
    #if MICROPY_PERSISTENT_CODE_LOAD
    if (f->kind == MP_WASM_PACK_KIND_MPY) {
        exec_mpy_into_module(module_obj, src_name, f->data, f->data_len);
        return;
    }
    #endif
    mp_raise_msg_varg(&mp_type_ValueError,
        MP_ERROR_TEXT("wasm pack file kind %d not supported"), (int)f->kind);
}

static void ensure_parent_packages(const char *full_name) {
    size_t len = strlen(full_name);
    for (size_t i = 0; i < len; ++i) {
        if (full_name[i] != '.') {
            continue;
        }
        qstr parent = qstr_from_strn(full_name, i);
        mp_obj_t pmod = mp_obj_new_module(parent);
        // Packages need __path__ so `import parent.child` works.
        mp_map_elem_t *el = mp_map_lookup(&mp_obj_module_get_globals(pmod)->map,
            MP_OBJ_NEW_QSTR(MP_QSTR___path__), MP_MAP_LOOKUP);
        if (el == NULL) {
            mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(pmod)),
                MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                mp_obj_new_str(qstr_str(parent), i));
        }
    }
}

static void link_module_to_parent(const char *dotted_name) {
    const char *dot = strrchr(dotted_name, '.');
    if (dot == NULL) {
        return;
    }
    qstr qparent = qstr_from_strn(dotted_name, (size_t)(dot - dotted_name));
    qstr qleaf = qstr_from_str(dot + 1);
    mp_map_elem_t *pel = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qparent), MP_MAP_LOOKUP);
    mp_map_elem_t *cel = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(dotted_name)), MP_MAP_LOOKUP);
    if (pel != NULL && cel != NULL && pel->value != MP_OBJ_NULL && cel->value != MP_OBJ_NULL) {
        mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(pel->value)),
            MP_OBJ_NEW_QSTR(qleaf), cel->value);
    }
}

static bool path_is_package_init(const char *path, size_t path_len) {
    size_t n = pack_logical_path_len(path, path_len);
    return (n == 8 && memcmp(path, "__init__", 8) == 0)
        || (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0);
}

static const char *stem_from_path(const char *path, char *buf, size_t buf_len) {
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    size_t n = strlen(base);
    if (n >= 5 && strcmp(base + n - 5, ".wasm") == 0) {
        n -= 5;
    }
    if (n >= 4 && strcmp(base + n - 4, ".aot") == 0) {
        n -= 4;
    }
    if (n == 8 && memcmp(base, "__init__", 8) == 0) {
        // package dir name is the parent folder
        const char *slash = NULL;
        for (const char *p = path; p < base; ++p) {
            if (*p == '/' || *p == '\\') {
                slash = p;
            }
        }
        if (slash != NULL) {
            const char *start = path;
            for (const char *p = path; p < slash; ++p) {
                if (*p == '/' || *p == '\\') {
                    start = p + 1;
                }
            }
            n = (size_t)(slash - start);
            if (n >= buf_len) {
                n = buf_len - 1;
            }
            memcpy(buf, start, n);
            buf[n] = '\0';
            return buf;
        }
    }
    if (n >= buf_len) {
        n = buf_len - 1;
    }
    memcpy(buf, base, n);
    buf[n] = '\0';
    return buf;
}

static mp_obj_t module_for_export_suffix(const char *pack_name, const char *suffix, uint16_t suffix_len) {
    if (suffix_len == 0 || (suffix_len == 1 && suffix[0] == '.')) {
        return mp_obj_new_module(qstr_from_str(pack_name));
    }
    vstr_t dotted;
    vstr_init(&dotted, strlen(pack_name) + suffix_len + 2);
    vstr_add_str(&dotted, pack_name);
    vstr_add_char(&dotted, '.');
    vstr_add_strn(&dotted, suffix, suffix_len);
    const char *name = vstr_null_terminated_str(&dotted);
    ensure_parent_packages(name);
    mp_obj_t mod = mp_obj_new_module(qstr_from_str(name));
    vstr_clear(&dotted);
    return mod;
}

static void bind_pack_exports(mp_obj_t root, mp_wasm_module_t *wmod, const char *pack_name, const mp_wasm_pack_info_t *info) {
    if (info != NULL && info->n_exports > 0) {
        for (uint32_t i = 0; i < info->n_exports; ++i) {
            const mp_wasm_pack_export_t *ex = &info->exports[i];
            if (ex->func_len == 0 || ex->export_len == 0) {
                continue;
            }
            vstr_t ename;
            vstr_init(&ename, ex->export_len + 1);
            vstr_add_strn(&ename, ex->export_name, ex->export_len);
            const char *export_c = vstr_null_terminated_str(&ename);
            // Introspect Wasm types (sig tag is a hint only; numeric i32/i64/f32/f64).
            if (!mp_wasm_module_numeric_export_arity(wmod, export_c, NULL, NULL)) {
                vstr_clear(&ename);
                continue;
            }
            qstr qexport = qstr_from_strn(ename.buf, ename.len);
            vstr_clear(&ename);

            mp_obj_t target = module_for_export_suffix(pack_name, ex->module, ex->module_len);
            mp_obj_t f = wasm_func_new(wmod, qexport);
            mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(target)),
                MP_OBJ_NEW_QSTR(qstr_from_strn(ex->func, ex->func_len)), f);
            (void)root;
        }
        return;
    }
    // No export table: bind all numeric exports on the pack root.
    bind_ctx_t bctx = { .py_mod = root, .wasm = wmod };
    mp_wasm_module_foreach_numeric_export(wmod, bind_export_cb, &bctx);
}

static mp_obj_t load_pack_from_parts(const uint8_t *code, uint32_t code_len,
    const uint8_t *meta, uint32_t meta_len, const char *path_hint, const char *name_override) {
    if (meta == NULL) {
        meta = code;
        meta_len = code_len;
    }
    char name_buf[MP_WASM_NAME_MAX + 1];
    const char *pack_name = name_override;
    {
        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        mp_wasm_pack_info_t peek;
        memset(&peek, 0, sizeof(peek));
        if (pack_name == NULL
            && mp_wasm_pack_find_section(meta, meta_len, &payload, &payload_len)
            && mp_wasm_pack_parse(payload, payload_len, &peek)
            && peek.name_len > 0) {
            size_t n = peek.name_len < sizeof(name_buf) - 1 ? peek.name_len : sizeof(name_buf) - 1;
            memcpy(name_buf, peek.name, n);
            name_buf[n] = '\0';
            pack_name = name_buf;
            mp_wasm_pack_info_free(&peek);
        } else {
            mp_wasm_pack_info_free(&peek);
        }
    }
    if (pack_name == NULL && path_hint != NULL) {
        pack_name = stem_from_path(path_hint, name_buf, sizeof(name_buf));
    }
    if (pack_name == NULL || pack_name[0] == '\0') {
        pack_name = "wasm_pack";
    }

    char err[128];
    mp_wasm_module_t *wmod = mp_wasm_module_load_ex(code, code_len, meta, meta_len,
        pack_name, path_hint, err, sizeof(err));
    if (wmod == NULL) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasm load: %s"), err);
    }
    mp_wasm_module_set_name(wmod, pack_name);

    mp_wasm_pack_info_t info;
    memset(&info, 0, sizeof(info));
    bool have_pack = false;
    {
        uint32_t blen = 0;
        const uint8_t *bytes = mp_wasm_module_meta_bytes(wmod, &blen);
        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        have_pack = mp_wasm_pack_find_section(bytes, blen, &payload, &payload_len)
            && mp_wasm_pack_parse(payload, payload_len, &info);
    }

    int32_t lc = 0;
    (void)mp_wasm_module_call0(wmod, "mp_pack_load", &lc, NULL, 0);

    qstr qpack = qstr_from_str(pack_name);
    mp_obj_t root = mp_obj_new_module(qpack);
    mp_obj_t wasm_obj = wrap_loaded(wmod);
    ((mp_obj_wasm_module_t *)MP_OBJ_TO_PTR(wasm_obj))->pack_name = qpack;

    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(root)),
        MP_OBJ_NEW_QSTR(MP_QSTR___wasm__), wasm_obj);
    // Root is always a package when loaded via load_pack.
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(root)),
        MP_OBJ_NEW_QSTR(MP_QSTR___path__),
        mp_obj_new_str(pack_name, strlen(pack_name)));

    bind_pack_exports(root, wmod, pack_name, have_pack ? &info : NULL);

    if (have_pack) {
        // Pick one file per logical module: best upy .mpy, else .py (ignore .pyc).
        uint32_t *best_idx = m_new(uint32_t, info.n_files ? info.n_files : 1);
        int *best_score = m_new(int, info.n_files ? info.n_files : 1);
        uint32_t n_best = 0;
        for (uint32_t i = 0; i < info.n_files; ++i) {
            const mp_wasm_pack_file_t *f = &info.files[i];
            if (f->kind != MP_WASM_PACK_KIND_PY
                && f->kind != MP_WASM_PACK_KIND_MPY
                && f->kind != MP_WASM_PACK_KIND_PYC) {
                continue;
            }
            int score = score_pack_file_for_upy_host(f);
            if (score < 0) {
                continue;
            }
            uint32_t slot = n_best;
            bool found = false;
            for (uint32_t j = 0; j < n_best; ++j) {
                const mp_wasm_pack_file_t *g = &info.files[best_idx[j]];
                if (pack_logical_eq(f->path, f->path_len, g->path, g->path_len)) {
                    slot = j;
                    found = true;
                    break;
                }
            }
            if (!found) {
                best_idx[n_best] = i;
                best_score[n_best] = score;
                n_best++;
            } else if (score > best_score[slot]) {
                best_idx[slot] = i;
                best_score[slot] = score;
            }
        }
        for (uint32_t j = 0; j < n_best; ++j) {
            const mp_wasm_pack_file_t *f = &info.files[best_idx[j]];
            vstr_t dotted;
            path_to_dotted(pack_name, strlen(pack_name), f->path, f->path_len, &dotted);
            const char *dotted_name = vstr_null_terminated_str(&dotted);
            qstr qmod = qstr_from_strn(dotted.buf, dotted.len);
            ensure_parent_packages(dotted_name);
            mp_obj_t mod = mp_obj_new_module(qmod);
            mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
                MP_OBJ_NEW_QSTR(MP_QSTR___wasm__), wasm_obj);
            if (path_is_package_init(f->path, f->path_len)) {
                mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
                    MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                    mp_obj_new_str(dotted_name, dotted.len));
            }
            vstr_t src_name;
            vstr_init(&src_name, f->path_len + 16);
            vstr_add_str(&src_name, pack_name);
            vstr_add_char(&src_name, ':');
            vstr_add_strn(&src_name, f->path, f->path_len);
            exec_pack_file_into_module(mod, vstr_null_terminated_str(&src_name), f);
            link_module_to_parent(dotted_name);
            vstr_clear(&src_name);
            vstr_clear(&dotted);
        }
    }

    mp_wasm_pack_info_free(&info);
    return root;
}

mp_obj_t mp_wasm_load_pack_path(const char *path, const char *name_override) {
    char err[128];
    vstr_t code;
    vstr_t meta;
    bool have_meta = false;
    vstr_t verify_path_storage;
    const char *verify_path = path;
    bool verify_path_owned = false;

    #if MICROPY_PY_WASM_AOT
    // Finder may hand us .aot; otherwise prefer sibling .aot next to .wasm.
    if (ends_with(path, ".aot")) {
        if (!mp_wasm_fetch(path, &code, err, sizeof(err))) {
            mp_raise_OSError_with_filename(MP_ENOENT, path);
        }
        vstr_t sib;
        if (replace_suffix(path, ".aot", ".wasm", &sib)) {
            if (mp_wasm_fetch(vstr_null_terminated_str(&sib), &meta, err, sizeof(err))) {
                have_meta = true;
            }
            vstr_clear(&sib);
        }
        if (!have_meta && replace_suffix(path, ".aot", ".mpack", &sib)) {
            if (mp_wasm_fetch(vstr_null_terminated_str(&sib), &meta, err, sizeof(err))) {
                have_meta = true;
            }
            vstr_clear(&sib);
        }
    } else if (ends_with(path, ".wasm")) {
        vstr_t aot_path;
        bool try_aot = replace_suffix(path, ".wasm", ".aot", &aot_path);
        if (try_aot && mp_wasm_fetch(vstr_null_terminated_str(&aot_path), &code, err, sizeof(err))) {
            // Execute AOT; keep .wasm bytes as metadata.
            if (!mp_wasm_fetch(path, &meta, err, sizeof(err))) {
                vstr_clear(&code);
                vstr_clear(&aot_path);
                mp_raise_OSError_with_filename(MP_ENOENT, path);
            }
            have_meta = true;
            vstr_init(&verify_path_storage, aot_path.len + 1);
            vstr_add_strn(&verify_path_storage, aot_path.buf, aot_path.len);
            verify_path = vstr_null_terminated_str(&verify_path_storage);
            verify_path_owned = true;
            vstr_clear(&aot_path);
        } else {
            if (try_aot) {
                vstr_clear(&aot_path);
            }
            if (!mp_wasm_fetch(path, &code, err, sizeof(err))) {
                mp_raise_OSError_with_filename(MP_ENOENT, path);
            }
        }
    } else
    #endif
    {
        if (!mp_wasm_fetch(path, &code, err, sizeof(err))) {
            mp_raise_OSError_with_filename(MP_ENOENT, path);
        }
    }

    mp_obj_t root = load_pack_from_parts(
        (const uint8_t *)code.buf, (uint32_t)code.len,
        have_meta ? (const uint8_t *)meta.buf : NULL,
        have_meta ? (uint32_t)meta.len : 0,
        verify_path, name_override);
    vstr_clear(&code);
    if (have_meta) {
        vstr_clear(&meta);
    }
    if (verify_path_owned) {
        vstr_clear(&verify_path_storage);
    }
    return root;
}

static mp_obj_t mod_wasm_load_pack(size_t n_args, const mp_obj_t *args) {
    const char *name_override = NULL;
    if (n_args >= 2 && args[1] != mp_const_none) {
        name_override = mp_obj_str_get_str(args[1]);
    }
    if (mp_obj_is_str(args[0])) {
        return mp_wasm_load_pack_path(mp_obj_str_get_str(args[0]), name_override);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    return load_pack_from_parts(bufinfo.buf, (uint32_t)bufinfo.len, NULL, 0, NULL, name_override);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_load_pack_obj, 1, 2, mod_wasm_load_pack);

static mp_obj_t mod_wasm_unload(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    size_t nlen = strlen(name);
    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;

    mp_map_elem_t *el = mp_map_lookup(map, MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        mp_map_elem_t *w = mp_map_lookup(&mp_obj_module_get_globals(el->value)->map,
            MP_OBJ_NEW_QSTR(MP_QSTR___wasm__), MP_MAP_LOOKUP);
        if (w != NULL && mp_obj_is_type(w->value, (mp_obj_type_t *)&mp_type_wasm_module)) {
            mp_obj_wasm_module_t *wo = MP_OBJ_TO_PTR(w->value);
            if (wo->mod) {
                int32_t lc = 0;
                (void)mp_wasm_module_call0(wo->mod, "mp_pack_unload", &lc, NULL, 0);
            }
            wasm_module_close(MP_OBJ_FROM_PTR(wo));
        }
    }

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

static mp_obj_t mod_wasm_import_wasm(mp_obj_t name_in) {
    return mp_wasm_import_wasm(mp_obj_str_get_str(name_in));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_import_wasm_obj, mod_wasm_import_wasm);

static mp_obj_t mod_wasm_import_hook(size_t n_args, const mp_obj_t *args) {
    mp_obj_t prev = MP_STATE_VM(mp_wasm_prev_import);
    if (prev == MP_OBJ_NULL) {
        prev = MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_n_kw(prev, n_args, 0, args);
        nlr_pop();
        return res;
    }

    mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
    if (!mp_obj_exception_match(exc, MP_OBJ_FROM_PTR(&mp_type_ImportError)) || wasm_import_hook_depth > 0) {
        nlr_jump(nlr.ret_val);
    }

    const char *name = mp_obj_str_get_str(args[0]);
    wasm_import_hook_depth++;
    nlr_buf_t nlr2;
    if (nlr_push(&nlr2) == 0) {
        (void)mp_wasm_import_wasm(name);
        nlr_pop();
        wasm_import_hook_depth--;
        return mp_call_function_n_kw(prev, n_args, 0, args);
    }
    wasm_import_hook_depth--;
    // Prefer the original ImportError when the finder misses.
    if (mp_obj_exception_match(MP_OBJ_FROM_PTR(nlr2.ret_val), MP_OBJ_FROM_PTR(&mp_type_ImportError))) {
        nlr_jump(nlr.ret_val);
    }
    nlr_jump(nlr2.ret_val);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_import_hook_obj, 1, 5, mod_wasm_import_hook);

static mp_obj_t mod_wasm_install_hook(void) {
    #if !MICROPY_CAN_OVERRIDE_BUILTINS
    mp_raise_NotImplementedError(MP_ERROR_TEXT("wasm.install_hook requires MICROPY_CAN_OVERRIDE_BUILTINS"));
    #else
    if (MP_STATE_VM(mp_wasm_prev_import) != MP_OBJ_NULL) {
        return mp_const_none;
    }
    mp_obj_t dest[2] = { MP_OBJ_NULL, MP_OBJ_NULL };
    mp_load_method_maybe(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, dest);
    MP_STATE_VM(mp_wasm_prev_import) =
        dest[0] != MP_OBJ_NULL ? dest[0] : MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, MP_OBJ_FROM_PTR(&mod_wasm_import_hook_obj));
    return mp_const_none;
    #endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_install_hook_obj, mod_wasm_install_hook);

static mp_obj_t mod_wasm_uninstall_hook(void) {
    #if !MICROPY_CAN_OVERRIDE_BUILTINS
    return mp_const_none;
    #else
    if (MP_STATE_VM(mp_wasm_prev_import) == MP_OBJ_NULL) {
        return mp_const_none;
    }
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, MP_STATE_VM(mp_wasm_prev_import));
    MP_STATE_VM(mp_wasm_prev_import) = MP_OBJ_NULL;
    wasm_import_hook_depth = 0;
    return mp_const_none;
    #endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_uninstall_hook_obj, mod_wasm_uninstall_hook);

static mp_obj_t mod_wasm_add_trust(mp_obj_t key_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(key_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_wasm_trust_add(bufinfo.buf, bufinfo.len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.add_trust: bad key or trust full"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_add_trust_obj, mod_wasm_add_trust);

static mp_obj_t mod_wasm_trust_clear(void) {
    mp_wasm_trust_clear();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_trust_clear_obj, mod_wasm_trust_clear);

static mp_obj_t mod_wasm_host_set(mp_obj_t slot_in, mp_obj_t callable) {
    int32_t slot = (int32_t)mp_obj_get_int(slot_in);
    if (!mp_wasm_host_set_slot(slot, callable)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm.host_set: bad slot or callable"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_host_set_obj, mod_wasm_host_set);

static mp_obj_t mod_wasm_host_get(mp_obj_t slot_in) {
    return mp_wasm_host_get_slot((int32_t)mp_obj_get_int(slot_in));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_host_get_obj, mod_wasm_host_get);

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
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_host_clear_obj, 0, 1, mod_wasm_host_clear);

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
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_alloc_obj, mod_wasm_mem_alloc);

static mp_obj_t mod_wasm_mem_free(mp_obj_t cookie_in) {
    if (!mp_wasm_mem_free((int32_t)mp_obj_get_int(cookie_in))) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_free: bad cookie"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_free_obj, mod_wasm_mem_free);

static mp_obj_t mod_wasm_mem_get(mp_obj_t cookie_in) {
    int32_t cookie = (int32_t)mp_obj_get_int(cookie_in);
    if (!mp_wasm_mem_valid(cookie)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_get: bad cookie"));
    }
    uint32_t n = 0;
    const uint8_t *p = mp_wasm_mem_data(cookie, &n);
    return mp_obj_new_bytes(p, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_mem_get_obj, mod_wasm_mem_get);

static mp_obj_t mod_wasm_mem_set(mp_obj_t cookie_in, mp_obj_t data_in) {
    int32_t cookie = (int32_t)mp_obj_get_int(cookie_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    if (!mp_wasm_mem_set(cookie, bufinfo.buf, (uint32_t)bufinfo.len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mem_set: bad cookie or OOM"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_mem_set_obj, mod_wasm_mem_set);

static mp_obj_t mod_wasm_mem_clear(void) {
    mp_wasm_mem_clear_all();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_mem_clear_obj, mod_wasm_mem_clear);

static mp_obj_t mod_wasm_handle_register(mp_obj_t obj) {
    int32_t h = mp_wasm_handle_register(obj);
    if (h <= 0) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return mp_obj_new_int(h);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_register_obj, mod_wasm_handle_register);

static mp_obj_t mod_wasm_handle_resolve(mp_obj_t handle_in) {
    return mp_wasm_handle_resolve((int32_t)mp_obj_get_int(handle_in));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_resolve_obj, mod_wasm_handle_resolve);

static mp_obj_t mod_wasm_handle_free(mp_obj_t handle_in) {
    if (!mp_wasm_handle_free((int32_t)mp_obj_get_int(handle_in))) {
        mp_raise_ValueError(MP_ERROR_TEXT("handle_free: bad handle"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_handle_free_obj, mod_wasm_handle_free);

static mp_obj_t mod_wasm_handle_clear(void) {
    mp_wasm_handle_clear_all();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_handle_clear_obj, mod_wasm_handle_clear);

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
static MP_DEFINE_CONST_FUN_OBJ_VAR(mod_wasm_c_call_obj, 2, mod_wasm_c_call);

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
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_c_call_attr_obj, 2, 3, mod_wasm_c_call_attr);

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
static MP_DEFINE_CONST_FUN_OBJ_VAR(mod_wasm_rs_call_obj, 2, mod_wasm_rs_call);

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
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_rs_call_attr_obj, 2, 3, mod_wasm_rs_call_attr);
#endif // MICROPY_PY_WASM_MATRIX

#if MICROPY_MODULE_BUILTIN_INIT
static mp_obj_t mod_wasm___init__(void) {
    mp_obj_list_init(&MP_STATE_VM(mp_wasm_path_obj), 0);
    MP_STATE_VM(mp_wasm_prev_import) = MP_OBJ_NULL;
    MP_STATE_VM(mp_wasm_host_slots) = MP_OBJ_NULL;
    MP_STATE_VM(mp_wasm_handles) = MP_OBJ_NULL;
    wasm_import_hook_depth = 0;
    mp_wasm_trust_clear();
    mp_wasm_host_clear_all();
    mp_wasm_mem_clear_all();
    mp_wasm_handle_clear_all();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm___init___obj, mod_wasm___init__);
#endif

static const mp_rom_map_elem_t mp_module_wasm_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_wasm) },
    #if MICROPY_MODULE_BUILTIN_INIT
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_wasm___init___obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_path), MP_ROM_PTR(&MP_STATE_VM(mp_wasm_path_obj)) },
    { MP_ROM_QSTR(MP_QSTR_VERIFY), MP_ROM_INT(MICROPY_WASM_VERIFY) },
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
