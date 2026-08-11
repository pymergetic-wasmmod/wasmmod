/* pymergetic.wasmmod.registry — shared ABI shapes. See SOURCETREE.md
 * "Faces" and docs/REGISTRY.md for the design these mirror. */
#ifndef PYMERGETIC_WASMMOD_REGISTRY_TYPES_H
#define PYMERGETIC_WASMMOD_REGISTRY_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which kind of artifact a module's exports live behind. RESIDENT covers
 * a statically-linked-in C/Rust module with no container at all — this
 * is what makes wasm "one-of" alongside elf/aot, not a hierarchy wasm
 * sits above. */
typedef enum {
    PM_WASMMOD_REGISTRY_CONTAINER_WASM = 0,
    PM_WASMMOD_REGISTRY_CONTAINER_AOT = 1,
    PM_WASMMOD_REGISTRY_CONTAINER_ELF = 2,
    PM_WASMMOD_REGISTRY_CONTAINER_RESIDENT = 3,
} pm_wasmmod_registry_container_kind_t;

/* Marshaling shape for one export — same taxonomy already used for the
 * Python export faces (SOURCETREE.md "Py export face"), reused here
 * rather than invented twice. */
typedef enum {
    PM_WASMMOD_REGISTRY_EXPORT_FN = 0,
    PM_WASMMOD_REGISTRY_EXPORT_MEM = 1,
    PM_WASMMOD_REGISTRY_EXPORT_OBJ = 2,
    PM_WASMMOD_REGISTRY_EXPORT_I64 = 3,
    PM_WASMMOD_REGISTRY_EXPORT_F32 = 4,
    PM_WASMMOD_REGISTRY_EXPORT_F64 = 5,
    PM_WASMMOD_REGISTRY_EXPORT_BUFPTR = 6,
} pm_wasmmod_registry_export_kind_t;

/* index + generation, passed by value everywhere — never a pointer. A
 * stale copy (module since unpublished, slot since reused) is always
 * safely detectable via the generation mismatch, never a
 * dangling-pointer footgun. {UINT32_MAX, 0} is the canonical invalid
 * handle, returned by pm_wasmmod_registry_publish on failure. */
typedef struct {
    uint32_t index;
    uint32_t generation;
} pm_wasmmod_registry_handle_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_REGISTRY_TYPES_H */
