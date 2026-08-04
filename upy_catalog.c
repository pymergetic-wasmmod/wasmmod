/*
 * Minimal micropython.* host catalog.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "extmod/wasmmod/upy_catalog.h"

#include <string.h>

#include "py/mphal.h"
#include "wasm_export.h"

static int upy_catalog_registered;

// WAMR: () -> i32
static int32_t upy_ticks_ms_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)mp_hal_ticks_ms();
}

static NativeSymbol upy_runtime_symbols[] = {
    { "ticks_ms", (void *)upy_ticks_ms_wasm, "()i", NULL },
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

typedef struct {
    const char *module;
    const char *func;
    void *addr;
} upy_elf_slot_t;

static const upy_elf_slot_t upy_elf_slots[] = {
    { "micropython.runtime", "ticks_ms", (void *)upy_ticks_ms_elf },
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
