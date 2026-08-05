/*
 * Minimal micropython.* host catalog.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "extmod/wasmmod/upy_catalog.h"

#include <string.h>

#include "pm_upy/features.h"
#include "py/mphal.h"
#include "wasm_export.h"

static int upy_catalog_registered;

// WAMR: () -> i32
static int32_t upy_ticks_ms_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)mp_hal_ticks_ms();
}

static int32_t upy_features_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)pm_upy_features();
}

static int32_t upy_has_wasm(wasm_exec_env_t exec_env, int32_t feat) {
    (void)exec_env;
    return pm_upy_has((pm_upy_feat_t)feat) ? 1 : 0;
}

static NativeSymbol upy_runtime_symbols[] = {
    { "ticks_ms", (void *)upy_ticks_ms_wasm, "()i", NULL },
    { "features", (void *)upy_features_wasm, "()i", NULL },
    { "has", (void *)upy_has_wasm, "(i)i", NULL },
};

bool mp_wasm_upy_catalog_register(void) {
    if (upy_catalog_registered) {
        return true;
    }
    if (!wasm_runtime_register_natives("micropython.runtime", upy_runtime_symbols,
            sizeof(upy_runtime_symbols) / sizeof(upy_runtime_symbols[0]))) {
        return false;
    }
    upy_catalog_registered = 1;
    return true;
}

#if MICROPY_PY_WASM_ELF

static uint32_t upy_ticks_ms_elf(void) {
    return (uint32_t)mp_hal_ticks_ms();
}

static uint32_t upy_features_elf(void) {
    return pm_upy_features();
}

static int32_t upy_has_elf(int32_t feat) {
    return pm_upy_has((pm_upy_feat_t)feat) ? 1 : 0;
}

typedef struct {
    const char *module;
    const char *func;
    void *addr;
} upy_elf_slot_t;

static const upy_elf_slot_t upy_elf_slots[] = {
    { "micropython.runtime", "ticks_ms", (void *)upy_ticks_ms_elf },
    { "micropython.runtime", "features", (void *)upy_features_elf },
    { "micropython.runtime", "has", (void *)upy_has_elf },
};

void *mp_wasm_upy_catalog_elf_lookup(const char *module, const char *func) {
    if (module == NULL || func == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(upy_elf_slots) / sizeof(upy_elf_slots[0]); ++i) {
        if (strcmp(module, upy_elf_slots[i].module) == 0
            && strcmp(func, upy_elf_slots[i].func) == 0) {
            return upy_elf_slots[i].addr;
        }
    }
    return NULL;
}

#endif // MICROPY_PY_WASM_ELF

#endif // MICROPY_PY_WASM
