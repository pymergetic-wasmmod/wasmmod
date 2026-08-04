/*
 * Minimal micropython.* host catalog (guest imports).
 * Overnight: micropython.runtime.ticks_ms → mp_hal_ticks_ms.
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_UPY_CATALOG_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_UPY_CATALOG_H

#include <stdbool.h>

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

// Register WAMR natives for known micropython.* slots.
bool mp_wasm_upy_catalog_register(void);

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM_ELF
// ELF import resolve: module "micropython.runtime", func "ticks_ms", …
void *mp_wasm_upy_catalog_elf_lookup(const char *module, const char *func);
#endif

#endif // MICROPY_PY_WASM
#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_UPY_CATALOG_H
