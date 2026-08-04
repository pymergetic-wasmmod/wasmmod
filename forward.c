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

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include <stdio.h>
#include <string.h>

#include "extmod/wasmmod/forward.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/runtime.h"
#include "wasm_export.h"

#include "extmod/wasmmod/alloc.h"

typedef struct mp_wasm_reg_entry_t {
    struct mp_wasm_reg_entry_t *next;
    mp_wasm_module_t *mod;
} mp_wasm_reg_entry_t;

// Fast path: stack argv when arity fits; else one malloc (rare / fat signatures).
#ifndef MP_WASM_FWD_STACK_ARGS
#define MP_WASM_FWD_STACK_ARGS (8)
#endif

typedef struct mp_wasm_fwd_t {
    struct mp_wasm_fwd_t *next;
    char *module;
    char *func;
    uint32_t nparams;
    uint32_t nresults;
    uint8_t *param_kinds;  // Wasm binary valtypes 0x7f..0x7c
    uint8_t *result_kinds;
    NativeSymbol sym;
    // Hot-path cache (invalidated when target pack is unloaded/replaced).
    mp_wasm_module_t *cached_mod;
    wasm_function_inst_t cached_fn;
} mp_wasm_fwd_t;

static mp_wasm_reg_entry_t *registry;
static mp_wasm_fwd_t *forwarders;

static void fwd_invalidate_mod(mp_wasm_module_t *mod) {
    if (mod == NULL) {
        return;
    }
    for (mp_wasm_fwd_t *f = forwarders; f != NULL; f = f->next) {
        if (f->cached_mod == mod) {
            f->cached_mod = NULL;
            f->cached_fn = NULL;
        }
    }
}

void mp_wasm_registry_add(mp_wasm_module_t *mod) {
    if (mod == NULL) {
        return;
    }
    for (mp_wasm_reg_entry_t *e = registry; e != NULL; e = e->next) {
        if (e->mod != NULL && strcmp(mp_wasm_module_name(e->mod), mp_wasm_module_name(mod)) == 0) {
            if (e->mod != mod) {
                fwd_invalidate_mod(e->mod);
            }
            e->mod = mod;
            return;
        }
    }
    mp_wasm_reg_entry_t *e = MICROPY_WASM_MALLOC(sizeof(*e));
    if (e == NULL) {
        return;
    }
    e->mod = mod;
    e->next = registry;
    registry = e;
}

void mp_wasm_registry_remove(mp_wasm_module_t *mod) {
    fwd_invalidate_mod(mod);
    mp_wasm_reg_entry_t **pp = &registry;
    while (*pp) {
        if ((*pp)->mod == mod) {
            mp_wasm_reg_entry_t *dead = *pp;
            *pp = dead->next;
            MICROPY_WASM_FREE(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}

mp_wasm_module_t *mp_wasm_registry_find(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (mp_wasm_reg_entry_t *e = registry; e != NULL; e = e->next) {
        if (e->mod != NULL && strcmp(mp_wasm_module_name(e->mod), name) == 0) {
            return e->mod;
        }
    }
    return NULL;
}

static char wamr_sig_char(uint8_t vt) {
    switch (vt) {
        case 0x7f: return 'i';
        case 0x7e: return 'I';
        case 0x7d: return 'f';
        case 0x7c: return 'F';
        default: return 0;
    }
}

static wasm_valkind_t vt_to_kind(uint8_t vt) {
    switch (vt) {
        case 0x7f: return WASM_I32;
        case 0x7e: return WASM_I64;
        case 0x7d: return WASM_F32;
        case 0x7c: return WASM_F64;
        default: return WASM_I32;
    }
}

static void forward_raw(wasm_exec_env_t exec_env, uint64_t *args) {
    mp_wasm_fwd_t *fwd = wasm_runtime_get_function_attachment(exec_env);
    if (fwd == NULL) {
        return;
    }

    wasm_val_t stack_args[MP_WASM_FWD_STACK_ARGS];
    wasm_val_t result_buf;
    wasm_val_t *vargs = NULL;
    wasm_val_t *results = NULL;
    bool vargs_heap = false;

    uint64_t *p = args;
    if (fwd->nparams > 0) {
        if (fwd->nparams <= MP_WASM_FWD_STACK_ARGS) {
            vargs = stack_args;
        } else {
            vargs = MICROPY_WASM_MALLOC(fwd->nparams * sizeof(*vargs));
            if (vargs == NULL) {
                return;
            }
            vargs_heap = true;
        }
        for (uint32_t i = 0; i < fwd->nparams; ++i) {
            vargs[i].kind = vt_to_kind(fwd->param_kinds[i]);
            switch (vargs[i].kind) {
                case WASM_I32: {
                    native_raw_get_arg(int32_t, v, p);
                    vargs[i].of.i32 = v;
                    break;
                }
                case WASM_I64: {
                    native_raw_get_arg(int64_t, v, p);
                    vargs[i].of.i64 = v;
                    break;
                }
                case WASM_F32: {
                    native_raw_get_arg(float, v, p);
                    vargs[i].of.f32 = v;
                    break;
                }
                case WASM_F64: {
                    native_raw_get_arg(double, v, p);
                    vargs[i].of.f64 = v;
                    break;
                }
                default:
                    break;
            }
        }
    }

    if (fwd->nresults > 0) {
        memset(&result_buf, 0, sizeof(result_buf));
        results = &result_buf;
    }

    // Resolve peer once; reuse function inst across calls.
    if (fwd->cached_mod == NULL || fwd->cached_fn == NULL) {
        fwd->cached_mod = mp_wasm_registry_find(fwd->module);
        fwd->cached_fn = fwd->cached_mod
            ? mp_wasm_module_lookup_fn(fwd->cached_mod, fwd->func)
            : NULL;
    }

    if (fwd->cached_mod != NULL && fwd->cached_fn != NULL) {
        (void)mp_wasm_module_call_fn(fwd->cached_mod, fwd->cached_fn,
            fwd->nparams, vargs, fwd->nresults, results, NULL, 0);
    }

    if (fwd->nresults == 1 && results != NULL) {
        switch (results[0].kind) {
            case WASM_I32: {
                native_raw_return_type(int32_t, args);
                native_raw_set_return(results[0].of.i32);
                break;
            }
            case WASM_I64: {
                native_raw_return_type(int64_t, args);
                native_raw_set_return(results[0].of.i64);
                break;
            }
            case WASM_F32: {
                native_raw_return_type(float, args);
                native_raw_set_return(results[0].of.f32);
                break;
            }
            case WASM_F64: {
                native_raw_return_type(double, args);
                native_raw_set_return(results[0].of.f64);
                break;
            }
            default:
                break;
        }
    }

    if (vargs_heap) {
        MICROPY_WASM_FREE(vargs);
    }
}

static bool fwd_exists(const char *module, const char *func) {
    for (mp_wasm_fwd_t *f = forwarders; f != NULL; f = f->next) {
        if (strcmp(f->module, module) == 0 && strcmp(f->func, func) == 0) {
            return true;
        }
    }
    return false;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = MICROPY_WASM_MALLOC(n);
    if (d) {
        memcpy(d, s, n);
    }
    return d;
}

static bool register_one(const char *module, const char *func,
    uint32_t nparams, const uint8_t *param_kinds,
    uint32_t nresults, const uint8_t *result_kinds,
    char *errbuf, size_t errbuf_len) {
    if (fwd_exists(module, func)) {
        return true;
    }
    // WAMR raw forwarders: 0 or 1 result (multi-result peer calls use Py→Wasm).
    if (nresults > 1) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "forwarder %s.%s: multi-result not supported", module, func);
        }
        return false;
    }
    for (uint32_t i = 0; i < nparams; ++i) {
        if (wamr_sig_char(param_kinds[i]) == 0) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "forwarder %s.%s: unsupported param type", module, func);
            }
            return false;
        }
    }
    if (nresults == 1 && wamr_sig_char(result_kinds[0]) == 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "forwarder %s.%s: unsupported result type", module, func);
        }
        return false;
    }

    mp_wasm_fwd_t *fwd = MICROPY_WASM_MALLOC(sizeof(*fwd));
    if (fwd == NULL) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "out of memory");
        }
        return false;
    }
    memset(fwd, 0, sizeof(*fwd));
    fwd->module = xstrdup(module);
    fwd->func = xstrdup(func);
    if (fwd->module == NULL || fwd->func == NULL) {
        MICROPY_WASM_FREE(fwd->module);
        MICROPY_WASM_FREE(fwd->func);
        MICROPY_WASM_FREE(fwd);
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "out of memory");
        }
        return false;
    }
    fwd->nparams = nparams;
    fwd->nresults = nresults;
    if (nparams > 0) {
        fwd->param_kinds = MICROPY_WASM_MALLOC(nparams);
        if (fwd->param_kinds == NULL) {
            MICROPY_WASM_FREE(fwd->module);
            MICROPY_WASM_FREE(fwd->func);
            MICROPY_WASM_FREE(fwd);
            return false;
        }
        memcpy(fwd->param_kinds, param_kinds, nparams);
    }
    if (nresults > 0) {
        fwd->result_kinds = MICROPY_WASM_MALLOC(nresults);
        if (fwd->result_kinds == NULL) {
            MICROPY_WASM_FREE(fwd->param_kinds);
            MICROPY_WASM_FREE(fwd->module);
            MICROPY_WASM_FREE(fwd->func);
            MICROPY_WASM_FREE(fwd);
            return false;
        }
        memcpy(fwd->result_kinds, result_kinds, nresults);
    }

    // Leave signature NULL. LLVM JIT (and AOT) call imports with a non-NULL
    // signature via a direct typed call that never sets exec_env->attachment
    // and uses the wrong ABI for register_natives_raw. NULL forces
    // aot_invoke_native → invoke_native_raw, which sets attachment. Param
    // kinds are validated above; pointer/$ conversion is unused here.
    fwd->sym.symbol = fwd->func;
    fwd->sym.func_ptr = (void *)forward_raw;
    fwd->sym.signature = NULL;
    fwd->sym.attachment = fwd;

    if (!wasm_runtime_register_natives_raw(fwd->module, &fwd->sym, 1)) {
        MICROPY_WASM_FREE(fwd->param_kinds);
        MICROPY_WASM_FREE(fwd->result_kinds);
        MICROPY_WASM_FREE(fwd->module);
        MICROPY_WASM_FREE(fwd->func);
        MICROPY_WASM_FREE(fwd);
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "register forwarder %s.%s failed", module, func);
        }
        return false;
    }
    fwd->next = forwarders;
    forwarders = fwd;
    return true;
}

// Parse Wasm type+import sections; return numeric kinds for (module, field).
static bool import_func_types(const uint8_t *wasm, uint32_t len, const char *module, const char *field,
    uint32_t *nparams_out, uint8_t **params_out, uint32_t *nresults_out, uint8_t **results_out) {
    *params_out = NULL;
    *results_out = NULL;
    *nparams_out = 0;
    *nresults_out = 0;

    const uint8_t *types_payload = NULL;
    uint32_t types_len = 0;
    const uint8_t *imports_payload = NULL;
    uint32_t imports_len = 0;
    if (!mp_wasm_find_section_id(wasm, len, 1, &types_payload, &types_len)
        || !mp_wasm_find_section_id(wasm, len, 2, &imports_payload, &imports_len)) {
        return false;
    }

    uint32_t n_types = 0;
    const uint8_t *tp = types_payload;
    const uint8_t *tend = types_payload + types_len;
    if (!mp_wasm_read_uleb(&tp, tend, &n_types)) {
        return false;
    }

    typedef struct {
        uint32_t nparams;
        uint32_t nresults;
        uint8_t *params;
        uint8_t *results;
        bool ok;
    } type_info_t;

    type_info_t *types = MICROPY_WASM_MALLOC(n_types * sizeof(*types));
    if (types == NULL) {
        return false;
    }
    memset(types, 0, n_types * sizeof(*types));

    bool parse_ok = true;
    for (uint32_t i = 0; i < n_types && parse_ok; ++i) {
        if (tp >= tend || *tp++ != 0x60) {
            parse_ok = false;
            break;
        }
        uint32_t np = 0, nr = 0;
        if (!mp_wasm_read_uleb(&tp, tend, &np)) {
            parse_ok = false;
            break;
        }
        uint8_t *params = NULL;
        if (np > 0) {
            params = MICROPY_WASM_MALLOC(np);
            if (params == NULL) {
                parse_ok = false;
                break;
            }
        }
        bool ok = true;
        for (uint32_t j = 0; j < np; ++j) {
            if (tp >= tend) {
                ok = false;
                break;
            }
            uint8_t vt = *tp++;
            params[j] = vt;
            if (wamr_sig_char(vt) == 0) {
                ok = false;
            }
        }
        if (!mp_wasm_read_uleb(&tp, tend, &nr)) {
            MICROPY_WASM_FREE(params);
            parse_ok = false;
            break;
        }
        uint8_t *results = NULL;
        if (nr > 0) {
            results = MICROPY_WASM_MALLOC(nr);
            if (results == NULL) {
                MICROPY_WASM_FREE(params);
                parse_ok = false;
                break;
            }
        }
        for (uint32_t j = 0; j < nr; ++j) {
            if (tp >= tend) {
                ok = false;
                break;
            }
            uint8_t vt = *tp++;
            results[j] = vt;
            if (wamr_sig_char(vt) == 0) {
                ok = false;
            }
        }
        types[i].nparams = np;
        types[i].nresults = nr;
        types[i].params = params;
        types[i].results = results;
        types[i].ok = ok && nr <= 1;
    }

    bool found = false;
    if (parse_ok) {
        const uint8_t *ip = imports_payload;
        const uint8_t *iend = imports_payload + imports_len;
        uint32_t n_imports = 0;
        if (mp_wasm_read_uleb(&ip, iend, &n_imports)) {
            for (uint32_t i = 0; i < n_imports; ++i) {
                uint32_t mlen = 0, flen = 0;
                if (!mp_wasm_read_uleb(&ip, iend, &mlen) || ip + mlen > iend) {
                    break;
                }
                const char *m = (const char *)ip;
                ip += mlen;
                if (!mp_wasm_read_uleb(&ip, iend, &flen) || ip + flen > iend) {
                    break;
                }
                const char *f = (const char *)ip;
                ip += flen;
                if (ip >= iend) {
                    break;
                }
                uint8_t kind = *ip++;
                if (kind == 0) {
                    uint32_t typeidx = 0;
                    if (!mp_wasm_read_uleb(&ip, iend, &typeidx)) {
                        break;
                    }
                    if (mlen == strlen(module) && flen == strlen(field)
                        && memcmp(m, module, mlen) == 0 && memcmp(f, field, flen) == 0
                        && typeidx < n_types && types[typeidx].ok) {
                        *nparams_out = types[typeidx].nparams;
                        *nresults_out = types[typeidx].nresults;
                        *params_out = types[typeidx].params;
                        *results_out = types[typeidx].results;
                        types[typeidx].params = NULL;
                        types[typeidx].results = NULL;
                        found = true;
                        break;
                    }
                } else if (kind == 1) {
                    if (ip >= iend) {
                        break;
                    }
                    ip++;
                    uint32_t flags = 0, initial = 0, maximum = 0;
                    if (!mp_wasm_read_uleb(&ip, iend, &flags) || !mp_wasm_read_uleb(&ip, iend, &initial)) {
                        break;
                    }
                    if ((flags & 1) && !mp_wasm_read_uleb(&ip, iend, &maximum)) {
                        break;
                    }
                } else if (kind == 2) {
                    uint32_t flags = 0, initial = 0, maximum = 0;
                    if (!mp_wasm_read_uleb(&ip, iend, &flags) || !mp_wasm_read_uleb(&ip, iend, &initial)) {
                        break;
                    }
                    if ((flags & 1) && !mp_wasm_read_uleb(&ip, iend, &maximum)) {
                        break;
                    }
                } else if (kind == 3) {
                    if (ip + 2 > iend) {
                        break;
                    }
                    ip += 2;
                } else {
                    break;
                }
            }
        }
    }

    for (uint32_t i = 0; i < n_types; ++i) {
        MICROPY_WASM_FREE(types[i].params);
        MICROPY_WASM_FREE(types[i].results);
    }
    MICROPY_WASM_FREE(types);
    return found;
}

bool mp_wasm_register_forwarders(const uint8_t *wasm, uint32_t len, char *errbuf, size_t errbuf_len) {
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (!mp_wasm_imports_find_section(wasm, len, &payload, &payload_len)) {
        return true;
    }
    mp_wasm_imports_info_t info;
    memset(&info, 0, sizeof(info));
    if (!mp_wasm_imports_parse(payload, payload_len, &info)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "bad wasmmod.imports section");
        }
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < info.n_imports; ++i) {
        const mp_wasm_import_t *im = &info.imports[i];
        size_t ml = im->module_len;
        size_t fl = im->func_len;
        if (ml > MP_WASM_NAME_MAX) {
            ml = MP_WASM_NAME_MAX;
        }
        if (fl > MP_WASM_NAME_MAX) {
            fl = MP_WASM_NAME_MAX;
        }
        char module[MP_WASM_NAME_MAX + 1];
        char func[MP_WASM_NAME_MAX + 1];
        memcpy(module, im->module, ml);
        module[ml] = '\0';
        memcpy(func, im->func, fl);
        func[fl] = '\0';

        // Host-provided natives — never install peer forwarders for these.
        if (strncmp(module, "micropython.", 12) == 0
            || strcmp(module, MP_WASM_HOST_MODULE) == 0
            || strcmp(module, MP_WASM_MODULE) == 0) {
            continue;
        }

        uint32_t nparams = 0, nresults = 0;
        uint8_t *params = NULL, *results = NULL;
        if (!import_func_types(wasm, len, module, func, &nparams, &params, &nresults, &results)) {
            // Default ()->i32 when type missing / non-numeric.
            nparams = 0;
            nresults = 1;
            results = MICROPY_WASM_MALLOC(1);
            if (results == NULL) {
                ok = false;
                break;
            }
            results[0] = 0x7f;
        }
        if (!register_one(module, func, nparams, params, nresults, results, errbuf, errbuf_len)) {
            ok = false;
            MICROPY_WASM_FREE(params);
            MICROPY_WASM_FREE(results);
            break;
        }
        MICROPY_WASM_FREE(params);
        MICROPY_WASM_FREE(results);
    }
    mp_wasm_imports_info_free(&info);
    return ok;
}

bool mp_wasm_connect_imports(const uint8_t *wasm, uint32_t len, char *errbuf, size_t errbuf_len) {
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (!mp_wasm_imports_find_section(wasm, len, &payload, &payload_len)) {
        return true;
    }
    mp_wasm_imports_info_t info;
    if (!mp_wasm_imports_parse(payload, payload_len, &info)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "bad wasmmod.imports");
        }
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < info.n_imports; ++i) {
        const mp_wasm_import_t *im = &info.imports[i];
        char module[MP_WASM_NAME_MAX + 1];
        size_t ml = im->module_len > MP_WASM_NAME_MAX ? MP_WASM_NAME_MAX : im->module_len;
        memcpy(module, im->module, ml);
        module[ml] = '\0';
        if (strncmp(module, "micropython.", 12) == 0
            || strcmp(module, MP_WASM_HOST_MODULE) == 0
            || strcmp(module, MP_WASM_MODULE) == 0) {
            continue;
        }
        if (mp_wasm_registry_find(module) == NULL) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "connect: pack '%s' not registered", module);
            }
            ok = false;
            break;
        }
    }
    mp_wasm_imports_info_free(&info);
    return ok;
}

#endif // MICROPY_PY_WASM
