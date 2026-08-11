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

/* The four primitive shapes a value crossing a container boundary can
 * be — deliberately the same four as wasm's own core value types
 * (WAMR's own wasm_val_t uses this exact kind+union shape), not a
 * separate encoding invented for the registry: the loader's wasm
 * trampolines build/read these directly against WAMR's call API, no
 * translation step at the one boundary where it would actually cost
 * something. See docs/REGISTRY.md "Value convention". */
typedef enum {
    PM_WASMMOD_REGISTRY_VALKIND_I32 = 0,
    PM_WASMMOD_REGISTRY_VALKIND_I64 = 1,
    PM_WASMMOD_REGISTRY_VALKIND_F32 = 2,
    PM_WASMMOD_REGISTRY_VALKIND_F64 = 3,
} pm_wasmmod_registry_valkind_t;

typedef struct {
    pm_wasmmod_registry_valkind_t kind;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
    } of;
} pm_wasmmod_registry_value_t;

/* The fixed prototype every cross-container `Fn` export conforms to
 * once resolved via pm_wasmmod_registry_resolve_native — args in,
 * results out, i32 status (0 == ok, matching every other status return
 * in this module). Same-artifact native-to-native calls never go
 * through this: those stay a direct, really-typed function pointer
 * resolved once via pm_wasmmod_registry_connect_import (see
 * SOURCETREE.md "Same-artifact cross-module calls"). This is only for
 * the genuinely cross-container case — today the loader's claimed wasm
 * trampolines, later elf/aot. */
typedef int32_t (*pm_wasmmod_registry_fn_t)(const pm_wasmmod_registry_value_t *args, uint32_t nargs,
    pm_wasmmod_registry_value_t *results, uint32_t nresults);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_REGISTRY_TYPES_H */
