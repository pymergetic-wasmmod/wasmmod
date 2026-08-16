/*
 * This file is part of wasmmod, https://github.com/pymergetic-wasmmod/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * pymergetic.wasmmod.guest — umbrella header (path == module).
 * Guests: #include "pymergetic/wasmmod/guest.h" with -Isrc (never src/ in the include).
 *
 * Guest pack ABI macros (Wasm / AOT-as-Wasm / ELF).
 *
 * PM_WASMMOD_GUEST:
 *   - auto 1 on __wasm__ / __wasm32__ / __wasm64__
 *   - set -DPM_WASMMOD_GUEST=1 for ELF ET_REL pack builds
 *   - 0 on the host (default)
 */
#ifndef PM_GUEST_H_
#define PM_GUEST_H_

#include <stddef.h>
#include <stdint.h>

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
#ifndef PM_WASMMOD_GUEST
#define PM_WASMMOD_GUEST 1
#endif
#define PM_WASMMOD_GUEST_WASM 1
#else
#ifndef PM_WASMMOD_GUEST
#define PM_WASMMOD_GUEST 0
#endif
#define PM_WASMMOD_GUEST_WASM 0
#endif

#if PM_WASMMOD_GUEST_WASM
#define MP_WASM_IMPORT_ATTR(module, name) \
    __attribute__((import_module(module), import_name(name)))
#else
#define MP_WASM_IMPORT_ATTR(module, name)
#endif

#define MP_WASM_IMPORT(module, ret, fn, ...) \
    MP_WASM_IMPORT_ATTR(module, #fn) ret fn(__VA_ARGS__)

#define MP_WASM_IMPORT_AS(module, wasm_name, ret, c_name, ...) \
    MP_WASM_IMPORT_ATTR(module, wasm_name) ret c_name(__VA_ARGS__)

/* Legacy wasm i32 cast — prefer pm_addr_t / pm_buf_t (docs/CALLGRAPH.md). */
#define MP_WASM_PTR(p) ((int32_t)(uintptr_t)(p))

#include "registry/__types__.h" /* IWYU pragma: keep — PM_MOD_CONNECT */
#include "boot/__types__.h"     /* IWYU pragma: keep — PM_MOD_BOOT_* */

/*
 * PM_MOD_EXPORT_C — C language face of host export registration.
 * RS language face: `PM_MOD_EXPORT_RS!` in guest.rs (same module, path == guest).
 * Both call pm_wasmmod_registry_mod_export (one table). Host: ctor /
 * .init_array. Guests: packer export table; this expands to nothing for now.
 * `mod` is the registry key (prefer full fqn).
 */
#if !PM_WASMMOD_GUEST
#define PM_MOD_EXPORT_C(mod, export_name, impl_fn, sig) \
    static void __attribute__((constructor)) pm_mod_export_reg_##impl_fn(void) { \
        (void)pm_wasmmod_registry_mod_export( \
            (const uint8_t *)#mod, (uint32_t)(sizeof(#mod) - 1), \
            (const uint8_t *)#export_name, (uint32_t)(sizeof(#export_name) - 1), \
            PM_WASMMOD_REGISTRY_EXPORT_FN, (void *)(impl_fn), \
            (const uint8_t *)#sig, (uint32_t)(sizeof(#sig) - 1)); \
    }
#else
#define PM_MOD_EXPORT_C(mod, export_name, impl_fn, sig) /* guest: pack table */
#endif

/*
 * PM_MOD_BOOT_C / PM_MOD_BOOTDEP_C / PM_MOD_BOOT_CHILD_C — lifecycle
 * registry (linker sections `pm_mod_boot` / `pm_mod_bootdep`).
 * Record lives in `pm_mod_boot` / `pm_mod_bootdep`. Every seat also emits a
 * constructor that calls add() (browser .init_array, unix libc, BIOS
 * crt0, UEFI .CRT$XCU). ELF walk of `__start_*` is extra; add() dedups.
 * RS: `PM_MOD_BOOT_RS!` / `PM_MOD_BOOTDEP_RS!` in guest.rs.
 * Python: `PM_MOD_BOOT` / `PM_MOD_BOOTDEP` / `PM_MOD_BOOT_CHILD` on
 * `pymergetic.wasmmod.guest` (same names; runtime `pm_mod_boot_add`).
 *
 * BOOTDEP: hard — `mod` is linked so `dep` must be too.
 * CHILD: parent names a submodule; skipped if the child TU is not linked.
 */
#define PM_MOD_BOOT_C(mod, init_fn, deinit_fn) \
    PM_MOD_BOOT_READY_C(mod, init_fn, deinit_fn, NULL)

#define PM_MOD_BOOT_CAT_(a, b) a##b
#define PM_MOD_BOOT_CAT(a, b) PM_MOD_BOOT_CAT_(a, b)

#define PM_MOD_BOOT_REG_(sym) \
    static void __attribute__((constructor)) PM_MOD_BOOT_CAT(pm_mod_boot_reg_, __COUNTER__)(void) { \
        (void)pm_mod_boot_add(&(sym)); \
    }
#define PM_MOD_BOOTDEP_REG_(sym) \
    static void __attribute__((constructor)) PM_MOD_BOOT_CAT(pm_mod_bootdep_reg_, __COUNTER__)(void) { \
        (void)pm_mod_bootdep_add(&(sym)); \
    }

#define PM_MOD_BOOT_READY_C(mod, init_fn, deinit_fn, ready_fn) \
    static const pm_mod_boot_t __attribute__((section("pm_mod_boot"), used, aligned(8))) \
        pm_mod_boot_##init_fn = { \
            #mod, (pm_mod_boot_init_fn)(init_fn), (deinit_fn), (ready_fn) \
        }; \
    PM_MOD_BOOT_REG_(pm_mod_boot_##init_fn)

#define PM_MOD_BOOTDEP_RECORD_(sym, mod, dep, flags) \
    static const pm_mod_bootdep_t __attribute__((section("pm_mod_bootdep"), used, aligned(8))) \
        sym = { #mod, #dep, (flags) }; \
    PM_MOD_BOOTDEP_REG_(sym)

#define PM_MOD_BOOTDEP_C(mod, dep) \
    PM_MOD_BOOTDEP_RECORD_(PM_MOD_BOOT_CAT(pm_mod_bootdep_, __COUNTER__), mod, dep, PM_MOD_BOOTDEP_HARD)

#define PM_MOD_BOOT_CHILD_C(mod, child) \
    PM_MOD_BOOTDEP_RECORD_(PM_MOD_BOOT_CAT(pm_mod_bootdep_, __COUNTER__), child, mod, PM_MOD_BOOTDEP_CHILD)

/*
 * Soft-connect one export into a typed or Bridge slot (hand-written
 * __imports__.h until PMM codegen). `out_slot` is void **; cast to
 * pm_wasmmod_registry_fn_t * or a really-typed fn pointer on Native edges.
 * Returns 1 on success, 0 on miss (same as pm_wasmmod_registry_connect_import).
 */
#define PM_MOD_CONNECT(fqn_lit, export_lit, out_slot) \
    pm_wasmmod_registry_connect_import( \
        (const uint8_t *)(fqn_lit), (uint32_t)(sizeof(fqn_lit) - 1), \
        (const uint8_t *)(export_lit), (uint32_t)(sizeof(export_lit) - 1), \
        (out_slot))

/*
 * PM_MOD_TEST_C — module test case (`__tests__.c`).
 * Case: int32_t (*)(void) — 0 pass, nonzero fail. Never a product export.
 * Host: constructor registers into ModEntry.tests.
 * Guest: packer scans this macro → wasm export + wasmmod.tests (MPTE).
 */
#if defined(PM_MOD_TESTS) && !PM_WASMMOD_GUEST
#define PM_MOD_TEST_C(mod, case_name, impl_fn) \
    static void __attribute__((constructor)) pm_mod_test_reg_##impl_fn(void) { \
        (void)pm_wasmmod_registry_test_register( \
            (const uint8_t *)#mod, (uint32_t)(sizeof(#mod) - 1), \
            (const uint8_t *)#case_name, (uint32_t)(sizeof(#case_name) - 1), \
            (pm_wasmmod_registry_test_fn_t)(impl_fn)); \
    }
#else
/* Packer scans the call site; keep a ref so -Wunused-function stays quiet
 * when this expands to nothing (guest TU / clangd without PM_MOD_TESTS). */
#define PM_MOD_TEST_C(mod, case_name, impl_fn) \
    static int32_t (*const pm_mod_test_keep_##impl_fn)(void) \
        __attribute__((unused)) = (impl_fn)
#endif

#endif /* PM_GUEST_H_ */
