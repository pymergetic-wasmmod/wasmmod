/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
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

#define MP_WASM_PTR(p) ((int32_t)(uintptr_t)(p))

/*
 * PM_MOD_EXPORT_C(module, export_name, impl_fn, c_type_signature):
 * Declarative marker only. dev/tools's face-export-discovery (pmm-parser)
 * scans this call site textually to build the pack's export table and
 * (when the signature is all-i32) its compact sig tag — see
 * dev/tools/src/pymergetic/wasmmod/tools/faces.py. Expands to nothing at
 * the C level for now; real same-artifact slot-backed-wrapper +
 * eager-connect registration (so private cross-module calls within one
 * compiled artifact can resolve without going through the loader/registry)
 * is separate, later work — see docs/SOURCETREE.md "Same-artifact calls
 * stay private" / the pm-mod-export-macro backlog item.
 */
#define PM_MOD_EXPORT_C(mod, export_name, impl_fn, sig) /* nothing */

#endif /* PM_GUEST_H_ */
