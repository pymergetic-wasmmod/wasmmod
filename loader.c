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

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM

#include <limits.h>
#include <string.h>

#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/loader.h"
#include "extmod/wasmmod/mod.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/runtime.h"
#include "extmod/wasmmod/verify.h"
#include "extmod/wasmmod/version.h"
#include "wasm_export.h"

#ifndef MP_WASM_LOADER_NAME_MAX
#define MP_WASM_LOADER_NAME_MAX MP_WASM_NAME_MAX
#endif

#ifndef MP_WASM_LOADER_CALL_ARGS_MAX
#define MP_WASM_LOADER_CALL_ARGS_MAX (8u)
#endif

static int loader_registered;

static bool loader_guest_bytes(wasm_exec_env_t exec_env, int32_t off, int32_t len, void **out) {
    if (len < 0 || out == NULL) {
        return false;
    }
    return mp_wasm_linear_from_exec(exec_env, (uint32_t)off, (uint32_t)len, out);
}

static bool loader_guest_name(wasm_exec_env_t exec_env, int32_t off, int32_t len,
    char *buf, size_t buf_sz) {
    if (len < 0 || (size_t)len >= buf_sz) {
        return false;
    }
    void *p = NULL;
    if (!loader_guest_bytes(exec_env, off, len, &p)) {
        return false;
    }
    memcpy(buf, p, (size_t)len);
    buf[len] = '\0';
    return true;
}

// Copy MICROPY_WASM_VERSION into guest linear memory. Returns nbytes (no NUL),
// or -1 if maxlen is too small / bad pointer.
static int32_t loader_version(wasm_exec_env_t exec_env, int32_t off, int32_t maxlen) {
    const char *ver = MICROPY_WASM_VERSION;
    size_t n = strlen(ver);
    if (maxlen < 0 || (size_t)maxlen < n) {
        return -1;
    }
    void *p = NULL;
    if (!loader_guest_bytes(exec_env, off, (int32_t)n, &p)) {
        return -1;
    }
    if (n > 0) {
        memcpy(p, ver, n);
    }
    return (int32_t)n;
}

static int32_t loader_mode(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)MICROPY_WASM_MODE_DEFAULT;
}

static int32_t loader_verify(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return mp_wasm_get_verify_enabled() ? 1 : 0;
}

static int32_t loader_trust_count(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)mp_wasm_trust_count();
}

// Dynamic pack export call (guest mirror of host wasm.c_call / rs_call).
// args_off → nargs × i32 in linear memory (ignored when nargs == 0).
// On failure returns INT32_MIN (not a normal export result for our smoke packs).
static int32_t loader_call_i32(wasm_exec_env_t exec_env,
    int32_t pack_off, int32_t pack_len,
    int32_t func_off, int32_t func_len,
    int32_t nargs, int32_t args_off) {
    char pack[MP_WASM_LOADER_NAME_MAX + 1];
    char func[MP_WASM_LOADER_NAME_MAX + 1];
    if (!loader_guest_name(exec_env, pack_off, pack_len, pack, sizeof(pack))
        || !loader_guest_name(exec_env, func_off, func_len, func, sizeof(func))) {
        return INT32_MIN;
    }
    if (nargs < 0 || (uint32_t)nargs > MP_WASM_LOADER_CALL_ARGS_MAX) {
        return INT32_MIN;
    }
    int32_t args_buf[MP_WASM_LOADER_CALL_ARGS_MAX];
    const int32_t *args = NULL;
    if (nargs > 0) {
        void *p = NULL;
        if (!loader_guest_bytes(exec_env, args_off, nargs * (int32_t)sizeof(int32_t), &p)) {
            return INT32_MIN;
        }
        memcpy(args_buf, p, (size_t)nargs * sizeof(int32_t));
        args = args_buf;
    }
    int32_t out = 0;
    if (mp_wasm_host_call_export_i32(pack, (size_t)pack_len, func, (size_t)func_len,
            (uint32_t)nargs, args, &out) != 0) {
        return INT32_MIN;
    }
    return out;
}

static NativeSymbol loader_symbols[] = {
    { "version", (void *)loader_version, "(ii)i", NULL },
    { "mode", (void *)loader_mode, "()i", NULL },
    { "verify", (void *)loader_verify, "()i", NULL },
    { "trust_count", (void *)loader_trust_count, "()i", NULL },
    { "call_i32", (void *)loader_call_i32, "(iiiiii)i", NULL },
};

bool mp_wasm_loader_register(void) {
    if (loader_registered) {
        return true;
    }
    loader_symbols[0].func_ptr = (void *)loader_version;
    loader_symbols[1].func_ptr = (void *)loader_mode;
    loader_symbols[2].func_ptr = (void *)loader_verify;
    loader_symbols[3].func_ptr = (void *)loader_trust_count;
    loader_symbols[4].func_ptr = (void *)loader_call_i32;
    if (!wasm_runtime_register_natives(MP_WASM_MODULE, loader_symbols,
            sizeof(loader_symbols) / sizeof(loader_symbols[0]))) {
        return false;
    }
    loader_registered = 1;
    return true;
}

#if MICROPY_PY_WASM_ELF
// ELF guests: pointer args replace Wasm linear offsets.

static int32_t elf_loader_mode(void) {
    return loader_mode(NULL);
}
static int32_t elf_loader_verify(void) {
    return loader_verify(NULL);
}
static int32_t elf_loader_trust_count(void) {
    return loader_trust_count(NULL);
}

static bool elf_loader_name(const void *ptr, int32_t len, char *buf, size_t buf_sz) {
    if (ptr == NULL || len < 0 || (size_t)len >= buf_sz) {
        return false;
    }
    memcpy(buf, ptr, (size_t)len);
    buf[len] = '\0';
    return true;
}

static int32_t elf_loader_version(void *buf, int32_t maxlen) {
    const char *ver = MICROPY_WASM_VERSION;
    size_t n = strlen(ver);
    if (buf == NULL || maxlen < 0 || (size_t)maxlen < n) {
        return -1;
    }
    if (n > 0) {
        memcpy(buf, ver, n);
    }
    return (int32_t)n;
}

static int32_t elf_loader_call_i32(const void *pack_ptr, int32_t pack_len,
    const void *func_ptr, int32_t func_len,
    int32_t nargs, const int32_t *args) {
    char pack[MP_WASM_LOADER_NAME_MAX + 1];
    char func[MP_WASM_LOADER_NAME_MAX + 1];
    if (!elf_loader_name(pack_ptr, pack_len, pack, sizeof(pack))
        || !elf_loader_name(func_ptr, func_len, func, sizeof(func))) {
        return INT32_MIN;
    }
    if (nargs < 0 || (uint32_t)nargs > MP_WASM_LOADER_CALL_ARGS_MAX) {
        return INT32_MIN;
    }
    if (nargs > 0 && args == NULL) {
        return INT32_MIN;
    }
    int32_t out = 0;
    if (mp_wasm_host_call_export_i32(pack, (size_t)pack_len, func, (size_t)func_len,
            (uint32_t)nargs, args, &out) != 0) {
        return INT32_MIN;
    }
    return out;
}

typedef struct {
    const char *name;
    void *addr;
} mp_wasm_elf_loader_native_t;

static const mp_wasm_elf_loader_native_t elf_loader_natives[] = {
    { "version", (void *)elf_loader_version },
    { "mode", (void *)elf_loader_mode },
    { "verify", (void *)elf_loader_verify },
    { "trust_count", (void *)elf_loader_trust_count },
    { "call_i32", (void *)elf_loader_call_i32 },
};

void *mp_wasm_loader_elf_lookup(const char *func) {
    if (func == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(elf_loader_natives) / sizeof(elf_loader_natives[0]); ++i) {
        if (strcmp(elf_loader_natives[i].name, func) == 0) {
            return elf_loader_natives[i].addr;
        }
    }
    return NULL;
}
#endif // MICROPY_PY_WASM_ELF

#endif // MICROPY_PY_WASM
