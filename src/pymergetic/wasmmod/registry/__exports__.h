/* pymergetic.wasmmod.registry — the one source of truth for native
 * module identity + exports. Every call direction (H->G / G->H / G->G /
 * H->H) and every impl language bottoms out at pm_wasmmod_registry_resolve_native
 * reading this table. See docs/REGISTRY.md for the design. */
#ifndef PYMERGETIC_WASMMOD_REGISTRY_EXPORT_H
#define PYMERGETIC_WASMMOD_REGISTRY_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#include "src/pymergetic/wasmmod/registry/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent; kept as an explicit entry point even though today it's a
 * no-op beyond the table's own static init, so a future per-arena/
 * per-pack init story has somewhere to land without a new symbol. */
void pm_wasmmod_registry_init(void);

/* Registers a module. `fqn` need not be nul-terminated or static — it's
 * copied. Returns the invalid handle ({UINT32_MAX, 0}) if `fqn` isn't
 * valid UTF-8. */
pm_wasmmod_registry_handle_t pm_wasmmod_registry_publish(const uint8_t *fqn_ptr, uint32_t fqn_len,
    pm_wasmmod_registry_container_kind_t container);

/* Tears the module down: frees its exports and its slot becomes eligible
 * for reuse by a future publish (with a bumped generation — old handles
 * into this slot become detectably stale, never silently alias the new
 * occupant). Returns 1 on success, 0 if the handle was already stale. */
int32_t pm_wasmmod_registry_unpublish(pm_wasmmod_registry_handle_t handle);

int32_t pm_wasmmod_registry_has(const uint8_t *fqn_ptr, uint32_t fqn_len);

/* Sets (or replaces) one export by name on an already-published module.
 * Returns 1 on success, 0 if the handle is stale or the name isn't
 * valid UTF-8. */
int32_t pm_wasmmod_registry_export_set(pm_wasmmod_registry_handle_t handle,
    const uint8_t *name_ptr, uint32_t name_len,
    pm_wasmmod_registry_export_kind_t kind, void *ptr);

/* The one universal lookup every call direction bottoms out at. Returns
 * NULL if the module or the export doesn't exist — never partial/stale
 * data. */
void *pm_wasmmod_registry_resolve_native(const uint8_t *fqn_ptr, uint32_t fqn_len,
    const uint8_t *export_name_ptr, uint32_t export_name_len);

/* Resolves once and writes the result into *out_slot — the "soft
 * connect" pattern: callers do this once at load/link time and reuse
 * *out_slot afterward instead of re-resolving by name on every call.
 * Returns 1 on success (out_slot written), 0 if not found (out_slot
 * left untouched). */
int32_t pm_wasmmod_registry_connect_import(const uint8_t *fqn_ptr, uint32_t fqn_len,
    const uint8_t *export_name_ptr, uint32_t export_name_len,
    void **out_slot);

/* Resolve + call in one step, using the pm_wasmmod_registry_fn_t
 * convention (see __types__.h). Returns -1 if the module/export isn't
 * found; otherwise whatever the resolved function itself returns. This
 * is the one call path every cross-container Fn export is reached
 * through — the loader's claimed wasm trampolines today. */
int32_t pm_wasmmod_registry_call(const uint8_t *fqn_ptr, uint32_t fqn_len,
    const uint8_t *export_name_ptr, uint32_t export_name_len,
    const pm_wasmmod_registry_value_t *args, uint32_t nargs,
    pm_wasmmod_registry_value_t *results, uint32_t nresults);

/* Calls `visit(token, ctx)` once per live PM_WASMMOD_REGISTRY_EXPORT_OBJ export's token,
 * while the table's internal lock is held. The registry never
 * interprets the token itself — it's an opaque void* end to end;
 * whichever embedder eventually attaches a GC (upy, e.g.) is the only
 * side that knows what it really is. This is the one hook that makes
 * this table upstream-GC-compatible without wasmmod linking against
 * MicroPython at all: see docs/REGISTRY.md "GC and the obj export
 * kind". */
void pm_wasmmod_registry_gc_visit(void (*visit)(void *token, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_REGISTRY_EXPORT_H */
