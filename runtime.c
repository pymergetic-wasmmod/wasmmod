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

// Feature gate comes from the build (-DMICROPY_PY_WASM=1 via extmod.mk).
// Do not include py/mpconfig.h here: this file is a thin WAMR wrapper and
// should stay usable for tooling/IDE analysis without the full port include
// path. Default off when the macro is unset.
#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include <string.h>

#include "extmod/wasmmod/forward.h"
#include "extmod/wasmmod/runtime.h"
#include "extmod/wasmmod/verify.h"
#include "wasm_export.h"

// Defined in host.c (needs the full MicroPython include path).
bool mp_wasm_host_register(void);
bool mp_wasm_loader_register(void);

#ifndef MICROPY_PY_WASM_AOT
#define MICROPY_PY_WASM_AOT (0)
#endif
#ifndef MICROPY_PY_WASM_JIT
#define MICROPY_PY_WASM_JIT (0)
#endif
#ifndef MICROPY_PY_WASM_FAST_JIT
#define MICROPY_PY_WASM_FAST_JIT (0)
#endif

// Port hooks: override in mpconfigport.h to plug a custom allocator or
// to observe exports (e.g. a host registry). Defaults keep this module
// usable on plain unix MicroPython with no host glue.
#include "extmod/wasmmod/alloc.h"
#ifndef MICROPY_WASM_EXPORT_PUBLISH
#define MICROPY_WASM_EXPORT_PUBLISH(module_name, export_name, fn_ptr) ((void)0)
#endif

#ifndef MICROPY_WASM_STACK_SIZE
#define MICROPY_WASM_STACK_SIZE (64u * 1024u)
#endif
#ifndef MICROPY_WASM_HEAP_SIZE
#define MICROPY_WASM_HEAP_SIZE (256u * 1024u)
#endif

#define MP_WASM_ERRBUF 128

struct mp_wasm_module_t {
    char name[MP_WASM_NAME_MAX + 1];
    uint8_t *buf;       // executable (.wasm or .aot)
    uint32_t buf_len;
    uint8_t *meta;      // pack/imports metadata; NULL ⇒ use buf
    uint32_t meta_len;
    bool meta_owned;    // meta is a separate allocation
    wasm_module_t module;
    wasm_module_inst_t inst;
    wasm_exec_env_t exec;
};

static int runtime_ready;

static void *mp_wasm_malloc_cb(size_t size) {
    return MICROPY_WASM_MALLOC(size);
}

static void *mp_wasm_realloc_cb(void *ptr, size_t size) {
    return MICROPY_WASM_REALLOC(ptr, size);
}

static void mp_wasm_free_cb(void *ptr) {
    MICROPY_WASM_FREE(ptr);
}

static void set_err(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    if (msg == NULL) {
        msg = "wasm error";
    }
    size_t n = strlen(msg);
    if (n >= errbuf_len) {
        n = errbuf_len - 1;
    }
    memcpy(errbuf, msg, n);
    errbuf[n] = '\0';
}

// Pick the strongest engine compiled into libiwasm (override later via
// wasm_runtime_set_default_running_mode / per-instance set_running_mode).
static RunningMode mp_wasm_default_running_mode(void) {
    #if MICROPY_PY_WASM_JIT && MICROPY_PY_WASM_FAST_JIT
    return Mode_Multi_Tier_JIT;
    #elif MICROPY_PY_WASM_JIT
    return Mode_LLVM_JIT;
    #elif MICROPY_PY_WASM_FAST_JIT
    return Mode_Fast_JIT;
    #else
    return Mode_Interp;
    #endif
}

bool mp_wasm_runtime_init(void) {
    if (runtime_ready) {
        return true;
    }
    RuntimeInitArgs init;
    memset(&init, 0, sizeof(init));
    init.mem_alloc_type = Alloc_With_Allocator;
    init.mem_alloc_option.allocator.malloc_func = (void *)mp_wasm_malloc_cb;
    init.mem_alloc_option.allocator.realloc_func = (void *)mp_wasm_realloc_cb;
    init.mem_alloc_option.allocator.free_func = (void *)mp_wasm_free_cb;
    {
        RunningMode mode = mp_wasm_default_running_mode();
        if (wasm_runtime_is_running_mode_supported(mode)) {
            init.running_mode = mode;
        }
    }
    if (!wasm_runtime_full_init(&init)) {
        return false;
    }
    // Guest imports (wasmmod.* / wasmmod.host.*) before any module instantiate.
    if (!mp_wasm_host_register() || !mp_wasm_loader_register()) {
        wasm_runtime_destroy();
        return false;
    }
    runtime_ready = 1;
    return true;
}

void mp_wasm_runtime_deinit(void) {
    if (!runtime_ready) {
        return;
    }
    wasm_runtime_destroy();
    runtime_ready = 0;
}

mp_wasm_module_t *mp_wasm_module_load_ex(const uint8_t *code, uint32_t code_len,
    const uint8_t *meta, uint32_t meta_len, const char *name, const char *path_hint,
    char *errbuf, size_t errbuf_len) {
    char local_err[MP_WASM_ERRBUF];
    if (errbuf == NULL) {
        errbuf = local_err;
        errbuf_len = sizeof(local_err);
    }
    errbuf[0] = '\0';

    if (!mp_wasm_runtime_init()) {
        set_err(errbuf, errbuf_len, "wasm runtime init failed");
        return NULL;
    }
    if (code == NULL || code_len == 0) {
        set_err(errbuf, errbuf_len, "empty wasm");
        return NULL;
    }
    if (meta == NULL) {
        meta = code;
        meta_len = code_len;
    }

    // Verify the bytes that will be instantiated (code), using path for .sig.
    if (!mp_wasm_verify_bytes(code, code_len, path_hint, errbuf, errbuf_len)) {
        return NULL;
    }

    #if !MICROPY_PY_WASM_AOT
    // Reject AOT magic when AOT support is not compiled in: \0aot / similar.
    if (code_len >= 4 && code[0] == 0x00 && code[1] == 'a' && code[2] == 'o' && code[3] == 't') {
        set_err(errbuf, errbuf_len, "AOT disabled (build with MICROPY_PY_WASM_AOT=1)");
        return NULL;
    }
    #endif

    mp_wasm_module_t *mod = MICROPY_WASM_MALLOC(sizeof(mp_wasm_module_t));
    if (mod == NULL) {
        set_err(errbuf, errbuf_len, "out of memory");
        return NULL;
    }
    memset(mod, 0, sizeof(*mod));
    if (name != NULL) {
        strncpy(mod->name, name, sizeof(mod->name) - 1);
    } else {
        strncpy(mod->name, "wasm", sizeof(mod->name) - 1);
    }

    mod->buf = MICROPY_WASM_MALLOC(code_len);
    if (mod->buf == NULL) {
        set_err(errbuf, errbuf_len, "out of memory");
        MICROPY_WASM_FREE(mod);
        return NULL;
    }
    memcpy(mod->buf, code, code_len);
    mod->buf_len = code_len;

    if (meta != code) {
        mod->meta = MICROPY_WASM_MALLOC(meta_len);
        if (mod->meta == NULL) {
            set_err(errbuf, errbuf_len, "out of memory");
            MICROPY_WASM_FREE(mod->buf);
            MICROPY_WASM_FREE(mod);
            return NULL;
        }
        memcpy(mod->meta, meta, meta_len);
        mod->meta_len = meta_len;
        mod->meta_owned = true;
    } else {
        mod->meta = mod->buf;
        mod->meta_len = mod->buf_len;
        mod->meta_owned = false;
    }

    // Guest→guest forwarders come from metadata (custom sections on .wasm).
    if (!mp_wasm_register_forwarders(mod->meta, mod->meta_len, errbuf, errbuf_len)) {
        if (mod->meta_owned) {
            MICROPY_WASM_FREE(mod->meta);
        }
        MICROPY_WASM_FREE(mod->buf);
        MICROPY_WASM_FREE(mod);
        return NULL;
    }

    mod->module = wasm_runtime_load(mod->buf, mod->buf_len, errbuf, (uint32_t)errbuf_len);
    if (mod->module == NULL) {
        if (mod->meta_owned) {
            MICROPY_WASM_FREE(mod->meta);
        }
        MICROPY_WASM_FREE(mod->buf);
        MICROPY_WASM_FREE(mod);
        return NULL;
    }

    mod->inst = wasm_runtime_instantiate(mod->module, MICROPY_WASM_STACK_SIZE, MICROPY_WASM_HEAP_SIZE, errbuf, (uint32_t)errbuf_len);
    if (mod->inst == NULL) {
        wasm_runtime_unload(mod->module);
        if (mod->meta_owned) {
            MICROPY_WASM_FREE(mod->meta);
        }
        MICROPY_WASM_FREE(mod->buf);
        MICROPY_WASM_FREE(mod);
        return NULL;
    }

    mod->exec = wasm_runtime_create_exec_env(mod->inst, MICROPY_WASM_STACK_SIZE);
    if (mod->exec == NULL) {
        set_err(errbuf, errbuf_len, "create exec env failed");
        wasm_runtime_deinstantiate(mod->inst);
        wasm_runtime_unload(mod->module);
        if (mod->meta_owned) {
            MICROPY_WASM_FREE(mod->meta);
        }
        MICROPY_WASM_FREE(mod->buf);
        MICROPY_WASM_FREE(mod);
        return NULL;
    }

    mp_wasm_registry_add(mod);
    return mod;
}

mp_wasm_module_t *mp_wasm_module_load(const uint8_t *bytes, uint32_t len, const char *name, char *errbuf, size_t errbuf_len) {
    return mp_wasm_module_load_ex(bytes, len, NULL, 0, name, NULL, errbuf, errbuf_len);
}

void mp_wasm_module_close(mp_wasm_module_t *mod) {
    if (mod == NULL) {
        return;
    }
    mp_wasm_registry_remove(mod);
    if (mod->exec) {
        wasm_runtime_destroy_exec_env(mod->exec);
    }
    if (mod->inst) {
        wasm_runtime_deinstantiate(mod->inst);
    }
    if (mod->module) {
        wasm_runtime_unload(mod->module);
    }
    if (mod->meta_owned && mod->meta) {
        MICROPY_WASM_FREE(mod->meta);
    }
    if (mod->buf) {
        MICROPY_WASM_FREE(mod->buf);
    }
    MICROPY_WASM_FREE(mod);
}

bool mp_wasm_valkind_is_numeric(wasm_valkind_t kind) {
    return kind == WASM_I32 || kind == WASM_I64 || kind == WASM_F32 || kind == WASM_F64;
}

static bool kinds_all_numeric(const wasm_valkind_t *kinds, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (!mp_wasm_valkind_is_numeric(kinds[i])) {
            return false;
        }
    }
    return true;
}

bool mp_wasm_module_func_types(mp_wasm_module_t *mod, const char *func,
    uint32_t *nparams_out, wasm_valkind_t **params_out,
    uint32_t *nresults_out, wasm_valkind_t **results_out) {
    if (params_out) {
        *params_out = NULL;
    }
    if (results_out) {
        *results_out = NULL;
    }
    if (mod == NULL || mod->inst == NULL || func == NULL) {
        return false;
    }
    wasm_function_inst_t f = wasm_runtime_lookup_function(mod->inst, func);
    if (f == NULL) {
        return false;
    }
    uint32_t np = wasm_func_get_param_count(f, mod->inst);
    uint32_t nr = wasm_func_get_result_count(f, mod->inst);
    wasm_valkind_t *params = NULL;
    wasm_valkind_t *results = NULL;
    if (np > 0) {
        params = MICROPY_WASM_MALLOC(np * sizeof(*params));
        if (params == NULL) {
            return false;
        }
        wasm_func_get_param_types(f, mod->inst, params);
        if (!kinds_all_numeric(params, np)) {
            MICROPY_WASM_FREE(params);
            return false;
        }
    }
    if (nr > 0) {
        results = MICROPY_WASM_MALLOC(nr * sizeof(*results));
        if (results == NULL) {
            MICROPY_WASM_FREE(params);
            return false;
        }
        wasm_func_get_result_types(f, mod->inst, results);
        if (!kinds_all_numeric(results, nr)) {
            MICROPY_WASM_FREE(params);
            MICROPY_WASM_FREE(results);
            return false;
        }
    }
    if (nparams_out) {
        *nparams_out = np;
    }
    if (nresults_out) {
        *nresults_out = nr;
    }
    if (params_out) {
        *params_out = params;
    } else {
        MICROPY_WASM_FREE(params);
    }
    if (results_out) {
        *results_out = results;
    } else {
        MICROPY_WASM_FREE(results);
    }
    return true;
}

wasm_function_inst_t mp_wasm_module_lookup_fn(mp_wasm_module_t *mod, const char *func) {
    if (mod == NULL || mod->inst == NULL || func == NULL) {
        return NULL;
    }
    return wasm_runtime_lookup_function(mod->inst, func);
}

bool mp_wasm_module_call_fn(mp_wasm_module_t *mod, wasm_function_inst_t fn,
    uint32_t nargs, wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results,
    char *errbuf, size_t errbuf_len) {
    char local_err[MP_WASM_ERRBUF];
    if (errbuf == NULL) {
        errbuf = local_err;
        errbuf_len = sizeof(local_err);
    }
    errbuf[0] = '\0';

    if (mod == NULL || mod->exec == NULL || fn == NULL) {
        set_err(errbuf, errbuf_len, "invalid module");
        return false;
    }
    // Trust caller arity on the hot path (forwarders match import types).
    (void)nargs;
    (void)nresults;
    uint32_t np = wasm_func_get_param_count(fn, mod->inst);
    uint32_t nr = wasm_func_get_result_count(fn, mod->inst);
    if (!wasm_runtime_call_wasm_a(mod->exec, fn, nr, results, np, args)) {
        const char *ex = wasm_runtime_get_exception(mod->inst);
        set_err(errbuf, errbuf_len, ex ? ex : "wasm call failed");
        return false;
    }
    return true;
}

bool mp_wasm_module_call_vals(mp_wasm_module_t *mod, const char *func,
    uint32_t nargs, wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results,
    char *errbuf, size_t errbuf_len) {
    char local_err[MP_WASM_ERRBUF];
    if (errbuf == NULL) {
        errbuf = local_err;
        errbuf_len = sizeof(local_err);
    }
    errbuf[0] = '\0';

    if (mod == NULL || mod->exec == NULL || func == NULL) {
        set_err(errbuf, errbuf_len, "invalid module");
        return false;
    }
    wasm_function_inst_t f = wasm_runtime_lookup_function(mod->inst, func);
    if (f == NULL) {
        set_err(errbuf, errbuf_len, "export not found");
        return false;
    }
    uint32_t np = wasm_func_get_param_count(f, mod->inst);
    uint32_t nr = wasm_func_get_result_count(f, mod->inst);
    if (nargs != np) {
        set_err(errbuf, errbuf_len, "wrong number of args");
        return false;
    }
    if (nresults < nr) {
        set_err(errbuf, errbuf_len, "result buffer too small");
        return false;
    }
    if (!mp_wasm_module_call_fn(mod, f, nargs, args, nresults, results, errbuf, errbuf_len)) {
        return false;
    }
    MICROPY_WASM_EXPORT_PUBLISH(mod->name, func, (void *)(uintptr_t)f);
    return true;
}

bool mp_wasm_module_call0(mp_wasm_module_t *mod, const char *func, int32_t *out_result, char *errbuf, size_t errbuf_len) {
    return mp_wasm_module_call_i32(mod, func, NULL, 0, out_result, errbuf, errbuf_len);
}

bool mp_wasm_module_call_i32(mp_wasm_module_t *mod, const char *func, const int32_t *args, uint32_t nargs, int32_t *out_result, char *errbuf, size_t errbuf_len) {
    wasm_val_t *vargs = NULL;
    if (nargs > 0) {
        vargs = MICROPY_WASM_MALLOC(nargs * sizeof(*vargs));
        if (vargs == NULL) {
            set_err(errbuf, errbuf_len, "out of memory");
            return false;
        }
        for (uint32_t i = 0; i < nargs; ++i) {
            vargs[i].kind = WASM_I32;
            vargs[i].of.i32 = args[i];
        }
    }
    wasm_val_t result;
    memset(&result, 0, sizeof(result));
    bool ok = mp_wasm_module_call_vals(mod, func, nargs, vargs, 1, &result, errbuf, errbuf_len);
    MICROPY_WASM_FREE(vargs);
    if (!ok) {
        return false;
    }
    if (out_result) {
        *out_result = (result.kind == WASM_I32) ? result.of.i32 : 0;
    }
    return true;
}

uint32_t mp_wasm_module_export_names(mp_wasm_module_t *mod, const char **names, uint32_t max_names) {
    if (mod == NULL || names == NULL || max_names == 0 || mod->module == NULL) {
        return 0;
    }
    int32_t n = wasm_runtime_get_export_count(mod->module);
    if (n <= 0) {
        return 0;
    }
    uint32_t written = 0;
    for (int32_t i = 0; i < n && written < max_names; ++i) {
        wasm_export_t ex;
        wasm_runtime_get_export_type(mod->module, i, &ex);
        if (ex.kind == WASM_IMPORT_EXPORT_KIND_FUNC && ex.name != NULL) {
            names[written++] = ex.name;
        }
    }
    return written;
}

const char *mp_wasm_module_name(const mp_wasm_module_t *mod) {
    return mod ? mod->name : "";
}

void mp_wasm_module_set_name(mp_wasm_module_t *mod, const char *name) {
    if (mod == NULL) {
        return;
    }
    memset(mod->name, 0, sizeof(mod->name));
    if (name != NULL) {
        strncpy(mod->name, name, sizeof(mod->name) - 1);
    }
}

const uint8_t *mp_wasm_module_bytes(const mp_wasm_module_t *mod, uint32_t *len_out) {
    if (mod == NULL) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out) {
        *len_out = mod->buf_len;
    }
    return mod->buf;
}

const uint8_t *mp_wasm_module_meta_bytes(const mp_wasm_module_t *mod, uint32_t *len_out) {
    if (mod == NULL) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out) {
        *len_out = mod->meta_len;
    }
    return mod->meta ? mod->meta : mod->buf;
}

void mp_wasm_module_foreach_numeric_export(mp_wasm_module_t *mod, mp_wasm_numeric_export_cb cb, void *ctx) {
    if (mod == NULL || mod->module == NULL || cb == NULL) {
        return;
    }
    int32_t n = wasm_runtime_get_export_count(mod->module);
    if (n <= 0) {
        return;
    }
    for (int32_t i = 0; i < n; ++i) {
        wasm_export_t ex;
        wasm_runtime_get_export_type(mod->module, i, &ex);
        if (ex.kind != WASM_IMPORT_EXPORT_KIND_FUNC || ex.name == NULL || ex.u.func_type == NULL) {
            continue;
        }
        if (strcmp(ex.name, "memory") == 0 || strcmp(ex.name, "__heap_base") == 0
            || strcmp(ex.name, "__data_end") == 0) {
            continue;
        }
        uint32_t np = wasm_func_type_get_param_count(ex.u.func_type);
        uint32_t nr = wasm_func_type_get_result_count(ex.u.func_type);
        bool ok = true;
        for (uint32_t j = 0; j < np && ok; ++j) {
            ok = mp_wasm_valkind_is_numeric(wasm_func_type_get_param_valkind(ex.u.func_type, j));
        }
        for (uint32_t j = 0; j < nr && ok; ++j) {
            ok = mp_wasm_valkind_is_numeric(wasm_func_type_get_result_valkind(ex.u.func_type, j));
        }
        if (ok) {
            cb(ex.name, np, nr, ctx);
        }
    }
}

bool mp_wasm_module_numeric_export_arity(mp_wasm_module_t *mod, const char *name,
    uint32_t *nparams_out, uint32_t *nresults_out) {
    uint32_t np = 0, nr = 0;
    if (!mp_wasm_module_func_types(mod, name, &np, NULL, &nr, NULL)) {
        return false;
    }
    if (nparams_out) {
        *nparams_out = np;
    }
    if (nresults_out) {
        *nresults_out = nr;
    }
    return true;
}

void mp_wasm_module_foreach_i32_export(mp_wasm_module_t *mod, mp_wasm_i32_export_cb cb, void *ctx) {
    // Legacy helper: only i32 params and at most one i32 result.
    if (mod == NULL || mod->module == NULL || cb == NULL) {
        return;
    }
    int32_t n = wasm_runtime_get_export_count(mod->module);
    for (int32_t i = 0; i < n; ++i) {
        wasm_export_t ex;
        wasm_runtime_get_export_type(mod->module, i, &ex);
        if (ex.kind != WASM_IMPORT_EXPORT_KIND_FUNC || ex.name == NULL || ex.u.func_type == NULL) {
            continue;
        }
        uint32_t np = wasm_func_type_get_param_count(ex.u.func_type);
        uint32_t nr = wasm_func_type_get_result_count(ex.u.func_type);
        if (nr > 1) {
            continue;
        }
        bool ok = true;
        for (uint32_t j = 0; j < np && ok; ++j) {
            ok = wasm_func_type_get_param_valkind(ex.u.func_type, j) == WASM_I32;
        }
        if (nr == 1 && wasm_func_type_get_result_valkind(ex.u.func_type, 0) != WASM_I32) {
            ok = false;
        }
        if (ok) {
            cb(ex.name, np, nr, ctx);
        }
    }
}

bool mp_wasm_module_i32_export_arity(mp_wasm_module_t *mod, const char *name, uint32_t *nparams_out) {
    uint32_t np = 0, nr = 0;
    wasm_valkind_t *params = NULL;
    wasm_valkind_t *results = NULL;
    if (!mp_wasm_module_func_types(mod, name, &np, &params, &nr, &results)) {
        return false;
    }
    bool ok = nr <= 1;
    for (uint32_t i = 0; i < np && ok; ++i) {
        ok = params[i] == WASM_I32;
    }
    if (ok && nr == 1) {
        ok = results[0] == WASM_I32;
    }
    MICROPY_WASM_FREE(params);
    MICROPY_WASM_FREE(results);
    if (!ok) {
        return false;
    }
    if (nparams_out) {
        *nparams_out = np;
    }
    return true;
}

wasm_module_inst_t mp_wasm_module_inst(mp_wasm_module_t *mod) {
    return mod != NULL ? mod->inst : NULL;
}

bool mp_wasm_linear_from_exec(wasm_exec_env_t exec_env, uint32_t off, uint32_t n, void **out) {
    if (out == NULL) {
        return false;
    }
    *out = NULL;
    if (exec_env == NULL) {
        return false;
    }
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    if (inst == NULL) {
        return false;
    }
    if (!wasm_runtime_validate_app_addr(inst, (uint64_t)off, (uint64_t)n)) {
        return false;
    }
    void *native = wasm_runtime_addr_app_to_native(inst, (uint64_t)off);
    if (native == NULL && n != 0) {
        return false;
    }
    *out = native;
    return true;
}

bool mp_wasm_module_linear(mp_wasm_module_t *mod, uint32_t off, uint32_t n, void **out) {
    if (mod == NULL || mod->exec == NULL) {
        return false;
    }
    return mp_wasm_linear_from_exec(mod->exec, off, n, out);
}

bool mp_wasm_module_mem_read(mp_wasm_module_t *mod, uint32_t off, uint32_t n, void *dst) {
    void *src;
    if (dst == NULL || !mp_wasm_module_linear(mod, off, n, &src)) {
        return false;
    }
    if (n > 0) {
        memcpy(dst, src, n);
    }
    return true;
}

bool mp_wasm_module_mem_write(mp_wasm_module_t *mod, uint32_t off, uint32_t n, const void *src) {
    void *dst;
    if (src == NULL || !mp_wasm_module_linear(mod, off, n, &dst)) {
        return false;
    }
    if (n > 0) {
        memcpy(dst, src, n);
    }
    return true;
}

uint32_t mp_wasm_module_mem_alloc(mp_wasm_module_t *mod, uint32_t n, void **native_out) {
    if (mod == NULL || mod->inst == NULL) {
        return 0;
    }
    void *native = NULL;
    uint64_t off = wasm_runtime_module_malloc(mod->inst, (uint64_t)n, &native);
    if (off == 0) {
        return 0;
    }
    if (native_out != NULL) {
        *native_out = native;
    }
    return (uint32_t)off;
}

void mp_wasm_module_mem_free(mp_wasm_module_t *mod, uint32_t off) {
    if (mod == NULL || mod->inst == NULL || off == 0) {
        return;
    }
    wasm_runtime_module_free(mod->inst, (uint64_t)off);
}

#endif // MICROPY_PY_WASM
