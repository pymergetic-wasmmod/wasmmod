/* pymergetic.wasmmod.loader — hand-written stand-in for what `cbindgen`
 * should emit by reading __impl__.rs. Delete once the real cbindgen
 * pipeline exists; this is training-scaffold, not the plan.
 *
 * Turns `.wasm` bytes into directly-callable pymergetic.wasmmod.registry
 * entries. This is the one module in the tree that knows WAMR exists —
 * see docs/REGISTRY.md "Value convention" and docs/SOURCETREE.md's
 * decision log for the design this implements. Returned/accepted
 * handles are exactly pm_wasmmod_registry_handle_t: a loaded module
 * *is* one registry entry, there's no separate "loader handle" concept
 * to keep in sync with it. */
#ifndef PYMERGETIC_WASMMOD_LOADER_EXPORT_H
#define PYMERGETIC_WASMMOD_LOADER_EXPORT_H

#include <stdint.h>

#include "src/pymergetic/wasmmod/registry/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-time setup: wasm_runtime_init() plus reserving the shared-heap
 * backing block (via pymergetic.util.mem) that every subsequently
 * loaded module's instance gets attached to for buffer/string
 * marshaling. Idempotent — safe to call more than once. Must be called
 * before the first pm_wasmmod_loader_load. Returns 0 on success, -1 on
 * failure. */
int32_t pm_wasmmod_loader_init(void);

/* Loads, instantiates, attaches the shared heap, enumerates exports,
 * and publishes one pymergetic.wasmmod.registry entry with one Fn
 * export per exported wasm function — each backed by a claimed slot
 * from the loader's small fixed adapter pool (see __impl__.rs). `fqn`
 * is the dotted name to publish under; `bytes` is copied, so the
 * caller's buffer need not outlive this call. Returns the invalid
 * handle ({UINT32_MAX, 0}) on any failure (bad module, instantiation
 * failure, adapter pool exhausted, ...) — never a partially-published
 * module. */
pm_wasmmod_registry_handle_t pm_wasmmod_loader_load(const uint8_t *fqn_ptr, uint32_t fqn_len,
    const uint8_t *bytes_ptr, uint32_t bytes_len);

/* Unpublishes the module from the registry, releases every adapter
 * slot its exports claimed, detaches the shared heap, deinstantiates,
 * and unloads. Returns 0 on success, -1 if the handle was already
 * stale (already unloaded, or never valid). */
int32_t pm_wasmmod_loader_unload(pm_wasmmod_registry_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_LOADER_EXPORT_H */
