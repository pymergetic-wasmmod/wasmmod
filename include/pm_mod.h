/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Face modules: sys.modules (unchanged). Our connect metadata: global
 * __pm_modules (same dict shape as sys.modules). Soft edges cache once.
 */

#ifndef PM_MOD_H_
#define PM_MOD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pm_common.h" /* PM_OK / PM_ERR / PM_ERR_ARG for the C API return codes below */

#ifdef __cplusplus
extern "C" {
#endif

/** Code container — not a module kind. */
typedef enum {
    PM_MOD_RESIDENT = 0,
    PM_MOD_WASM = 1,
    PM_MOD_AOT = 2,
    PM_MOD_ELF = 3,
} pm_mod_container_t;

/** One export published into __pm_modules[name].native. */
typedef struct pm_mod_export {
    const char *name;
    void *ptr;
} pm_mod_export_t;

/**
 * Host soft import (kernel/resident → anywhere).
 * After connect (or lazy first get): slot holds cached fn / trampoline ptr.
 */
typedef struct pm_mod_import {
    const char *module;
    const char *func;
    void *slot; /* cached call edge; NULL until connect */
} pm_mod_import_t;

#define PM_MOD_EXPORT(name_) \
    {                        \
        .name = #name_, .ptr = NULL \
    }

#define PM_MOD_IMPORT(mod_, func_) \
    {                              \
        .module = (mod_), .func = (func_), .slot = NULL \
    }

/**
 * Ensure global `__pm_modules` exists (empty dict, builtins override).
 * Called from mp_init (weak no-op if mod.c is not linked).
 */
void pm_mod_init(void);

/**
 * Ensure __pm_modules[name] record. Does not mutate the face module.
 */
int pm_mod_publish(const char *name, pm_mod_container_t container,
    const pm_mod_export_t *exports, uint32_t n_exports);

/** Update one native export on the __pm_modules record. */
int pm_mod_export_set(const char *module, const char *func, void *fn);

/**
 * Drop __pm_modules[name] entirely (container + native dict). Symmetric
 * counterpart to pm_mod_publish; called on unload alongside the
 * sys.modules cleanup so no entry outlives its face module.
 */
int pm_mod_unpublish(const char *name);

/** Resolve native fn from __pm_modules[module].native[func]. */
void *pm_mod_resolve_native(const char *module, const char *func);

/**
 * Bind one import: look up __pm_modules once, cache into imp->slot.
 * Returns 0 if slot filled, -1 if peer missing.
 */
int pm_mod_connect_import(pm_mod_import_t *imp);

/** Bind many imports (batch connect). */
void pm_mod_connect_imports(pm_mod_import_t *imps, uint32_t n);

/**
 * Cached getter; lazy-connects on first use if slot still NULL.
 * Hot path after connect: return imp->slot only.
 */
void *pm_mod_import_get(pm_mod_import_t *imp);

#define pm_mod_import_as(T, imp) ((T)pm_mod_import_get(imp))

/** True if face exists in sys.modules/builtins or __pm_modules has a record. */
bool pm_mod_has(const char *name);

/**
 * Guest border helpers — hide i32 under the hood.
 * Guest heap allocs go through host malloc (Metal TLSF when linked).
 */
void *pm_mod_border_malloc(size_t n);
void pm_mod_border_free(void *p);
void *pm_mod_border_realloc(void *p, size_t n);

/**
 * Wire guest PM_MOD_NEED / wasmmod.imports for a loaded pack.
 * Resolves each peer via __pm_modules and/or pack __pack__; installs native
 * trampolines for __pm_modules peers. Fails if any peer is missing.
 * pack_instance is mp_pack_t *. Requires MICROPY_PY_WASM.
 */
int pm_mod_connect_guest(void *pack_instance);

/** Guest soft-need (pack → anywhere). Tooling emits a wasm import; host
 * `pm_mod_connect_guest` binds it to the peer export (border trampoline).
 * Same resolve target as PM_MOD_IMPORT — not a second registry. */
#define PM_MOD_NEED(sym, mod_, func_) \
    /* guest ABI: declare sym as imported; host wires (mod_, func_) */

#ifdef __cplusplus
}
#endif

#endif /* PM_MOD_H_ */
