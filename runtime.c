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
#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#include "extmod/wasmmod/format/common/format.h"
#if MICROPY_PY_WASM_ELF
#include "extmod/wasmmod/format/elf/load.h"
#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/loader.h"
#include "extmod/wasmmod/pack.h"
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
    uint8_t *buf;       // executable (.wasm / .aot / .elf bytes)
    uint32_t buf_len;
    uint8_t *meta;      // pack/imports metadata; NULL ⇒ use buf
    uint32_t meta_len;
    bool meta_owned;    // meta is a separate allocation
    wasm_module_t module;
    wasm_module_inst_t inst;
    wasm_exec_env_t exec;
#if MICROPY_PY_WASM_ELF
    mp_wasm_elf_image_t *elf; // non-NULL ⇒ in-tree ELF engine (no WAMR)
#endif
};

#if MICROPY_PY_WASM_ELF
static void *elf_resolve_import(const char *name, void *ctx);
void mp_wasm_elf_plt_invalidate(mp_wasm_module_t *mod);
#endif

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

    mp_wasm_artifact_kind_t kind = mp_wasm_artifact_kind(code, code_len);

    #if !MICROPY_PY_WASM_AOT
    if (kind == MP_WASM_KIND_AOT) {
        set_err(errbuf, errbuf_len, "AOT disabled (build with MICROPY_PY_WASM_AOT=1)");
        return NULL;
    }
    #endif
    #if !MICROPY_PY_WASM_ELF
    if (kind == MP_WASM_KIND_ELF) {
        set_err(errbuf, errbuf_len, "ELF disabled (build with MICROPY_PY_WASM_ELF=1)");
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

    // Guest→guest forwarders come from metadata (same for Wasm/AOT/ELF).
    if (!mp_wasm_register_forwarders(mod->meta, mod->meta_len, errbuf, errbuf_len)) {
        goto fail_mod;
    }

#if MICROPY_PY_WASM_ELF
    if (kind == MP_WASM_KIND_ELF) {
        if (!mp_wasm_elf_image_load(mod->buf, mod->buf_len, elf_resolve_import, mod,
                &mod->elf, errbuf, errbuf_len)) {
            goto fail_mod;
        }
        mp_wasm_registry_add(mod);
        return mod;
    }
#endif

    mod->module = wasm_runtime_load(mod->buf, mod->buf_len, errbuf, (uint32_t)errbuf_len);
    if (mod->module == NULL) {
        goto fail_mod;
    }

    mod->inst = wasm_runtime_instantiate(mod->module, MICROPY_WASM_STACK_SIZE, MICROPY_WASM_HEAP_SIZE, errbuf, (uint32_t)errbuf_len);
    if (mod->inst == NULL) {
        wasm_runtime_unload(mod->module);
        mod->module = NULL;
        goto fail_mod;
    }

    mod->exec = wasm_runtime_create_exec_env(mod->inst, MICROPY_WASM_STACK_SIZE);
    if (mod->exec == NULL) {
        set_err(errbuf, errbuf_len, "create exec env failed");
        wasm_runtime_deinstantiate(mod->inst);
        wasm_runtime_unload(mod->module);
        mod->inst = NULL;
        mod->module = NULL;
        goto fail_mod;
    }

    mp_wasm_registry_add(mod);
    return mod;

fail_mod:
#if MICROPY_PY_WASM_ELF
    if (mod->elf) {
        mp_wasm_elf_image_free(mod->elf);
        mod->elf = NULL;
    }
#endif
    if (mod->meta_owned) {
        MICROPY_WASM_FREE(mod->meta);
    }
    MICROPY_WASM_FREE(mod->buf);
    MICROPY_WASM_FREE(mod);
    return NULL;
}

mp_wasm_module_t *mp_wasm_module_load(const uint8_t *bytes, uint32_t len, const char *name, char *errbuf, size_t errbuf_len) {
    return mp_wasm_module_load_ex(bytes, len, NULL, 0, name, NULL, errbuf, errbuf_len);
}

void mp_wasm_module_close(mp_wasm_module_t *mod) {
    if (mod == NULL) {
        return;
    }
    mp_wasm_registry_remove(mod);
#if MICROPY_PY_WASM_ELF
    mp_wasm_elf_plt_invalidate(mod);
    if (mod->elf) {
        mp_wasm_elf_image_free(mod->elf);
        mod->elf = NULL;
    }
#endif
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


#if MICROPY_PY_WASM_ELF
// Resolve i32 arity from pack export sig tag (0..8 = N i32 args → i32).
// SIG_AUTO / missing → assume 0 params, 1 i32 result when symbol exists.
static bool elf_pack_sig_arity(mp_wasm_module_t *mod, const char *func,
    uint32_t *nparams_out, uint32_t *nresults_out) {
    uint32_t np = 0;
    uint32_t nr = 1;
    const uint8_t *payload = NULL;
    uint32_t plen = 0;
    if (mod->meta && mp_wasm_pack_find_section(mod->meta, mod->meta_len, &payload, &plen)) {
        mp_wasm_pack_info_t info;
        if (mp_wasm_pack_parse(payload, plen, &info)) {
            for (uint32_t i = 0; i < info.n_exports; ++i) {
                const mp_wasm_pack_export_t *ex = &info.exports[i];
                if (ex->export_len == strlen(func)
                    && memcmp(ex->export_name, func, ex->export_len) == 0) {
                    if (ex->sig <= 8) {
                        np = ex->sig;
                        nr = 1;
                    }
                    break;
                }
            }
            mp_wasm_pack_info_free(&info);
        }
    }
    if (nparams_out) {
        *nparams_out = np;
    }
    if (nresults_out) {
        *nresults_out = nr;
    }
    return true;
}

// ELF→Wasm PLT: fixed C stubs (no JIT). Each slot holds a peer + export name;
// stubs take up to 8 i32 args (System V unused regs ignored) and return i32.
#ifndef MP_WASM_ELF_PLT_SLOTS
#define MP_WASM_ELF_PLT_SLOTS (32)
#endif

typedef struct {
    bool used;
    mp_wasm_module_t *mod;
    char func[MP_WASM_NAME_MAX + 1];
    uint32_t nargs;
} mp_wasm_elf_plt_slot_t;

static mp_wasm_elf_plt_slot_t elf_plt_slots[MP_WASM_ELF_PLT_SLOTS];

static int32_t elf_plt_dispatch(unsigned idx, int32_t a0, int32_t a1, int32_t a2, int32_t a3,
    int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    if (idx >= MP_WASM_ELF_PLT_SLOTS || !elf_plt_slots[idx].used || elf_plt_slots[idx].mod == NULL) {
        return 0;
    }
    mp_wasm_elf_plt_slot_t *s = &elf_plt_slots[idx];
    wasm_function_inst_t fn = mp_wasm_module_lookup_fn(s->mod, s->func);
    if (fn == NULL) {
        return 0;
    }
    wasm_val_t args[8];
    wasm_val_t result;
    memset(args, 0, sizeof(args));
    memset(&result, 0, sizeof(result));
    const int32_t av[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
    uint32_t n = s->nargs;
    if (n > 8) {
        n = 8;
    }
    for (uint32_t i = 0; i < n; ++i) {
        args[i].kind = WASM_I32;
        args[i].of.i32 = av[i];
    }
    if (!mp_wasm_module_call_fn(s->mod, fn, n, args, 1, &result, NULL, 0)) {
        return 0;
    }
    return result.of.i32;
}

#define MP_WASM_ELF_PLT_STUB(i) \
    static int32_t elf_plt_stub_##i(int32_t a0, int32_t a1, int32_t a2, int32_t a3, \
        int32_t a4, int32_t a5, int32_t a6, int32_t a7) { \
        return elf_plt_dispatch((unsigned)(i), a0, a1, a2, a3, a4, a5, a6, a7); \
    }

MP_WASM_ELF_PLT_STUB(0)  MP_WASM_ELF_PLT_STUB(1)  MP_WASM_ELF_PLT_STUB(2)  MP_WASM_ELF_PLT_STUB(3)
MP_WASM_ELF_PLT_STUB(4)  MP_WASM_ELF_PLT_STUB(5)  MP_WASM_ELF_PLT_STUB(6)  MP_WASM_ELF_PLT_STUB(7)
MP_WASM_ELF_PLT_STUB(8)  MP_WASM_ELF_PLT_STUB(9)  MP_WASM_ELF_PLT_STUB(10) MP_WASM_ELF_PLT_STUB(11)
MP_WASM_ELF_PLT_STUB(12) MP_WASM_ELF_PLT_STUB(13) MP_WASM_ELF_PLT_STUB(14) MP_WASM_ELF_PLT_STUB(15)
MP_WASM_ELF_PLT_STUB(16) MP_WASM_ELF_PLT_STUB(17) MP_WASM_ELF_PLT_STUB(18) MP_WASM_ELF_PLT_STUB(19)
MP_WASM_ELF_PLT_STUB(20) MP_WASM_ELF_PLT_STUB(21) MP_WASM_ELF_PLT_STUB(22) MP_WASM_ELF_PLT_STUB(23)
MP_WASM_ELF_PLT_STUB(24) MP_WASM_ELF_PLT_STUB(25) MP_WASM_ELF_PLT_STUB(26) MP_WASM_ELF_PLT_STUB(27)
MP_WASM_ELF_PLT_STUB(28) MP_WASM_ELF_PLT_STUB(29) MP_WASM_ELF_PLT_STUB(30) MP_WASM_ELF_PLT_STUB(31)

#undef MP_WASM_ELF_PLT_STUB

typedef int32_t (*mp_wasm_elf_plt_fn_t)(int32_t, int32_t, int32_t, int32_t,
    int32_t, int32_t, int32_t, int32_t);

static mp_wasm_elf_plt_fn_t elf_plt_fns[MP_WASM_ELF_PLT_SLOTS] = {
    elf_plt_stub_0,  elf_plt_stub_1,  elf_plt_stub_2,  elf_plt_stub_3,
    elf_plt_stub_4,  elf_plt_stub_5,  elf_plt_stub_6,  elf_plt_stub_7,
    elf_plt_stub_8,  elf_plt_stub_9,  elf_plt_stub_10, elf_plt_stub_11,
    elf_plt_stub_12, elf_plt_stub_13, elf_plt_stub_14, elf_plt_stub_15,
    elf_plt_stub_16, elf_plt_stub_17, elf_plt_stub_18, elf_plt_stub_19,
    elf_plt_stub_20, elf_plt_stub_21, elf_plt_stub_22, elf_plt_stub_23,
    elf_plt_stub_24, elf_plt_stub_25, elf_plt_stub_26, elf_plt_stub_27,
    elf_plt_stub_28, elf_plt_stub_29, elf_plt_stub_30, elf_plt_stub_31,
};

void mp_wasm_elf_plt_invalidate(mp_wasm_module_t *mod) {
    if (mod == NULL) {
        return;
    }
    for (unsigned i = 0; i < MP_WASM_ELF_PLT_SLOTS; ++i) {
        if (elf_plt_slots[i].used && elf_plt_slots[i].mod == mod) {
            elf_plt_slots[i].used = false;
            elf_plt_slots[i].mod = NULL;
            elf_plt_slots[i].func[0] = '\0';
            elf_plt_slots[i].nargs = 0;
        }
    }
}

static void *elf_plt_alloc(mp_wasm_module_t *peer, const char *func, uint32_t nargs) {
    for (unsigned i = 0; i < MP_WASM_ELF_PLT_SLOTS; ++i) {
        if (elf_plt_slots[i].used
            && elf_plt_slots[i].mod == peer
            && strcmp(elf_plt_slots[i].func, func) == 0) {
            return (void *)elf_plt_fns[i];
        }
    }
    for (unsigned i = 0; i < MP_WASM_ELF_PLT_SLOTS; ++i) {
        if (!elf_plt_slots[i].used) {
            elf_plt_slots[i].used = true;
            elf_plt_slots[i].mod = peer;
            size_t n = strlen(func);
            if (n > MP_WASM_NAME_MAX) {
                n = MP_WASM_NAME_MAX;
            }
            memcpy(elf_plt_slots[i].func, func, n);
            elf_plt_slots[i].func[n] = '\0';
            elf_plt_slots[i].nargs = nargs > 8 ? 8 : nargs;
            return (void *)elf_plt_fns[i];
        }
    }
    return NULL;
}

static bool elf_is_host_module(const char *module) {
    return strncmp(module, "micropython.", 12) == 0
        || strcmp(module, MP_WASM_HOST_MODULE) == 0
        || strcmp(module, MP_WASM_MODULE) == 0;
}

static void *elf_resolve_native(const char *module, const char *func) {
    if (strcmp(module, MP_WASM_HOST_MODULE) == 0) {
        return mp_wasm_host_elf_lookup(func);
    }
    if (strcmp(module, MP_WASM_MODULE) == 0) {
        return mp_wasm_loader_elf_lookup(func);
    }
    return NULL; // micropython.* not wired for ELF
}

static void *elf_resolve_import(const char *name, void *ctx) {
    mp_wasm_module_t *importer = (mp_wasm_module_t *)ctx;
    if (importer == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (!mp_wasm_imports_find_section(importer->meta, importer->meta_len, &payload, &payload_len)) {
        return NULL;
    }
    mp_wasm_imports_info_t info;
    memset(&info, 0, sizeof(info));
    if (!mp_wasm_imports_parse(payload, payload_len, &info)) {
        return NULL;
    }
    void *addr = NULL;
    size_t name_len = strlen(name);
    for (uint32_t i = 0; i < info.n_imports; ++i) {
        const mp_wasm_import_t *im = &info.imports[i];
        if ((size_t)im->func_len != name_len || memcmp(im->func, name, name_len) != 0) {
            continue;
        }
        char module[MP_WASM_NAME_MAX + 1];
        size_t ml = im->module_len > MP_WASM_NAME_MAX ? MP_WASM_NAME_MAX : im->module_len;
        memcpy(module, im->module, ml);
        module[ml] = '\0';
        if (elf_is_host_module(module)) {
            addr = elf_resolve_native(module, name);
            break;
        }
        mp_wasm_module_t *peer = mp_wasm_registry_find(module);
        if (peer == NULL) {
            break;
        }
        if (peer->elf != NULL) {
            addr = mp_wasm_elf_lookup(peer->elf, name);
            break;
        }
        if (peer->inst != NULL) {
            uint32_t np = 0, nr = 1;
            elf_pack_sig_arity(peer, name, &np, &nr);
            (void)nr;
            // Prefer Wasm type arity when available.
            uint32_t wnp = 0, wnr = 0;
            wasm_valkind_t *wp = NULL, *wr = NULL;
            if (mp_wasm_module_func_types(peer, name, &wnp, &wp, &wnr, &wr)) {
                np = wnp;
                MICROPY_WASM_FREE(wp);
                MICROPY_WASM_FREE(wr);
            }
            addr = elf_plt_alloc(peer, name, np);
            break;
        }
        break;
    }
    mp_wasm_imports_info_free(&info);
    return addr;
}

static bool elf_func_types(mp_wasm_module_t *mod, const char *func,
    uint32_t *nparams_out, wasm_valkind_t **params_out,
    uint32_t *nresults_out, wasm_valkind_t **results_out) {
    if (params_out) {
        *params_out = NULL;
    }
    if (results_out) {
        *results_out = NULL;
    }
    if (mp_wasm_elf_lookup(mod->elf, func) == NULL) {
        return false;
    }
    uint32_t np = 0, nr = 1;
    elf_pack_sig_arity(mod, func, &np, &nr);
    wasm_valkind_t *params = NULL;
    wasm_valkind_t *results = NULL;
    if (np > 0) {
        params = MICROPY_WASM_MALLOC(np * sizeof(*params));
        if (params == NULL) {
            return false;
        }
        for (uint32_t i = 0; i < np; ++i) {
            params[i] = WASM_I32;
        }
    }
    if (nr > 0) {
        results = MICROPY_WASM_MALLOC(nr * sizeof(*results));
        if (results == NULL) {
            MICROPY_WASM_FREE(params);
            return false;
        }
        for (uint32_t i = 0; i < nr; ++i) {
            results[i] = WASM_I32;
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

// System V x86_64: integer args pass in registers regardless of the callee's
// declared arity, so casting to a fixed-arity i32(...) signature and calling
// is safe as long as nargs stays within the register budget (<= 8 here,
// matching the pack sig range in pack.h: 0..8 = N i32 args -> i32).
#ifndef MP_WASM_ELF_MAX_ARGS
#define MP_WASM_ELF_MAX_ARGS (8)
#endif

static bool elf_invoke_i32(void *fn, uint32_t nargs, const wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results, char *errbuf, size_t errbuf_len) {
    if (fn == NULL) {
        set_err(errbuf, errbuf_len, "elf: null function");
        return false;
    }
    if (nargs > MP_WASM_ELF_MAX_ARGS) {
        set_err(errbuf, errbuf_len, "elf: too many args (max 8)");
        return false;
    }
    if (nresults > 1) {
        set_err(errbuf, errbuf_len, "elf: multi-result not supported");
        return false;
    }
    int32_t a[MP_WASM_ELF_MAX_ARGS] = {0};
    for (uint32_t i = 0; i < nargs; ++i) {
        a[i] = (args != NULL) ? args[i].of.i32 : 0;
    }
    int32_t rv = 0;
    switch (nargs) {
        case 0:
            rv = ((int32_t (*)(void))fn)();
            break;
        case 1:
            rv = ((int32_t (*)(int32_t))fn)(a[0]);
            break;
        case 2:
            rv = ((int32_t (*)(int32_t, int32_t))fn)(a[0], a[1]);
            break;
        case 3:
            rv = ((int32_t (*)(int32_t, int32_t, int32_t))fn)(a[0], a[1], a[2]);
            break;
        case 4:
            rv = ((int32_t (*)(int32_t, int32_t, int32_t, int32_t))fn)(a[0], a[1], a[2], a[3]);
            break;
        case 5:
            rv = ((int32_t (*)(int32_t, int32_t, int32_t, int32_t, int32_t))fn)(
                a[0], a[1], a[2], a[3], a[4]);
            break;
        case 6:
            rv = ((int32_t (*)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t))fn)(
                a[0], a[1], a[2], a[3], a[4], a[5]);
            break;
        case 7:
            rv = ((int32_t (*)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t))fn)(
                a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
            break;
        default:
            rv = ((int32_t (*)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t))fn)(
                a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
            break;
    }
    if (nresults > 0 && results != NULL) {
        results[0].kind = WASM_I32;
        results[0].of.i32 = rv;
    }
    return true;
}

static bool elf_call_vals(mp_wasm_module_t *mod, const char *func,
    uint32_t nargs, wasm_val_t *args,
    uint32_t nresults, wasm_val_t *results,
    char *errbuf, size_t errbuf_len) {
    void *fn = mp_wasm_elf_lookup(mod->elf, func);
    if (fn == NULL) {
        set_err(errbuf, errbuf_len, "elf: export not found");
        return false;
    }
    uint32_t np = 0, nr = 1;
    elf_pack_sig_arity(mod, func, &np, &nr);
    if (nargs != np) {
        set_err(errbuf, errbuf_len, "elf: bad arity");
        return false;
    }
    return elf_invoke_i32(fn, nargs, args, nresults, results, errbuf, errbuf_len);
}
#endif

bool mp_wasm_module_func_types(mp_wasm_module_t *mod, const char *func,
    uint32_t *nparams_out, wasm_valkind_t **params_out,
    uint32_t *nresults_out, wasm_valkind_t **results_out) {
    if (params_out) {
        *params_out = NULL;
    }
    if (results_out) {
        *results_out = NULL;
    }
    if (mod == NULL || func == NULL) {
        return false;
    }
#if MICROPY_PY_WASM_ELF
    if (mod->elf != NULL) {
        return elf_func_types(mod, func, nparams_out, params_out, nresults_out, results_out);
    }
#endif
    if (mod->inst == NULL) {
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
    if (mod == NULL || func == NULL) {
        return NULL;
    }
#if MICROPY_PY_WASM_ELF
    if (mod->elf != NULL) {
        return (wasm_function_inst_t)mp_wasm_elf_lookup(mod->elf, func);
    }
#endif
    if (mod->inst == NULL) {
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

    if (mod == NULL || fn == NULL) {
        set_err(errbuf, errbuf_len, "invalid module");
        return false;
    }
#if MICROPY_PY_WASM_ELF
    if (mod->elf != NULL) {
        // fn is a raw function pointer (see mp_wasm_module_lookup_fn), not a
        // WAMR object — trust caller-supplied arity, same as the WAMR hot
        // path below (forwarders already validated nparams/nresults at
        // register_forwarders time from the *importing* module's Wasm type).
        return elf_invoke_i32((void *)fn, nargs, args, nresults, results, errbuf, errbuf_len);
    }
#endif
    if (mod->exec == NULL) {
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

    if (mod == NULL || func == NULL) {
        set_err(errbuf, errbuf_len, "invalid module");
        return false;
    }
#if MICROPY_PY_WASM_ELF
    if (mod->elf != NULL) {
        return elf_call_vals(mod, func, nargs, args, nresults, results, errbuf, errbuf_len);
    }
#endif
    if (mod->exec == NULL) {
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

#if MICROPY_PY_WASM_ELF
typedef struct {
    mp_wasm_module_t *mod;
    mp_wasm_numeric_export_cb cb;
    void *ctx;
} elf_foreach_ctx_t;

static void elf_foreach_cb(const char *name, void *addr, void *ctx_in) {
    (void)addr;
    elf_foreach_ctx_t *c = ctx_in;
    uint32_t np = 0, nr = 1;
    elf_pack_sig_arity(c->mod, name, &np, &nr);
    c->cb(name, np, nr, c->ctx);
}
#endif

void mp_wasm_module_foreach_numeric_export(mp_wasm_module_t *mod, mp_wasm_numeric_export_cb cb, void *ctx) {
    if (mod == NULL || cb == NULL) {
        return;
    }
#if MICROPY_PY_WASM_ELF
    if (mod->elf != NULL) {
        elf_foreach_ctx_t c = { .mod = mod, .cb = cb, .ctx = ctx };
        mp_wasm_elf_foreach_func(mod->elf, elf_foreach_cb, &c);
        return;
    }
#endif
    if (mod->module == NULL) {
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
