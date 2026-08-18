/* pymergetic.wasmmod.registry — shared ABI shapes. See SOURCETREE.md
 * "Faces" and docs/REGISTRY.md for the design these mirror. */
#ifndef PYMERGETIC_WASMMOD_REGISTRY_TYPES_H
#define PYMERGETIC_WASMMOD_REGISTRY_TYPES_H

#include <stddef.h>
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

/* Module `__tests__.*` case — 0 = pass, nonzero = fail. Not an export face. */
typedef int32_t (*pm_wasmmod_registry_test_fn_t)(void);

/* Bench case — runs `iterations` ops, 0 = ok (same status convention). The
 * clock hook returns a monotonic u64 tick; both mirror the registry's Rust
 * face (registry/__impl__.rs), kept here so generated __exports__.h and any C
 * consumer share one ABI. */
typedef int32_t (*pm_wasmmod_registry_bench_fn_t)(uint64_t iterations);
typedef uint64_t (*pm_wasmmod_registry_bench_clock_t)(void);

/* Loader hook: run a guest pack test by wasm export name (fqn, export). */
typedef int32_t (*pm_wasmmod_registry_wasm_test_runner_t)(const uint8_t *fqn, uint32_t fqn_len,
    const uint8_t *export_name, uint32_t export_len);
/* Same loader hook for guest pack benches — carries the iterations word that
 * a `pm_wasmmod_registry_bench_fn_t` takes, so a guest bench is timed like a
 * native one once the loader can call its wasm export. */
typedef int32_t (*pm_wasmmod_registry_wasm_bench_runner_t)(const uint8_t *fqn, uint32_t fqn_len,
    const uint8_t *export_name, uint32_t export_len, uint64_t iterations);

/* Portable address — see docs/CALLGRAPH.md.
 *   SPACE_NATIVE (0): off is a host VA (ELF / resident / unisolated).
 *   SPACE_SHARED (1): off is a WAMR shared-heap app address.
 *   SPACE_MODULE (2): off is that module instance's linear-memory app address.
 * Public faces pass pm_addr_t / pm_buf_t; only the wasm bridge widens to i32. */
#define PM_ADDR_SPACE_NATIVE  0u
#define PM_ADDR_SPACE_SHARED  1u
#define PM_ADDR_SPACE_MODULE  2u

typedef struct {
    uint32_t space;
    uint64_t off;
} pm_addr_t;

typedef struct {
    pm_addr_t ptr;
    uint32_t len;
} pm_buf_t;

static inline __attribute__((unused)) pm_addr_t pm_addr_from_native(const void *p) {
    pm_addr_t a;
    a.space = PM_ADDR_SPACE_NATIVE;
    a.off = (uint64_t)(uintptr_t)p;
    return a;
}

static inline __attribute__((unused)) void *pm_addr_as_native(pm_addr_t a) {
    if (a.space != PM_ADDR_SPACE_NATIVE) {
        return (void *)0;
    }
    return (void *)(uintptr_t)a.off;
}

static inline __attribute__((unused)) pm_buf_t pm_buf_from_native(const void *p, uint32_t len) {
    pm_buf_t b;
    b.ptr = pm_addr_from_native(p);
    b.len = len;
    return b;
}

static inline __attribute__((unused)) pm_wasmmod_registry_value_t pm_wasmmod_registry_value_i32(int32_t v) {
    pm_wasmmod_registry_value_t x;
    x.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
    x.of.i32 = v;
    return x;
}

/* Callees of PM_MOD_EXPORT_C / PM_MOD_CONNECT / PM_MOD_TEST_C in guest.h.
 * Handwritten ABI — generated __exports__.h repeats these for consumers. */
int32_t pm_wasmmod_registry_mod_export(const uint8_t *fqn, uint32_t fqn_len, const uint8_t *name,
    uint32_t name_len, pm_wasmmod_registry_export_kind_t kind, void *fn, const uint8_t *sig,
    uint32_t sig_len);
int32_t pm_wasmmod_registry_connect_import(const uint8_t *fqn, uint32_t fqn_len, const uint8_t *name,
    uint32_t name_len, void **out);
int32_t pm_wasmmod_registry_test_register(const uint8_t *fqn, uint32_t fqn_len, const uint8_t *name,
    uint32_t name_len, pm_wasmmod_registry_test_fn_t fn);

/* Bench face — same shapes as the Rust registry/__impl__.rs. The clock fill
 * is installed once per seat via pm_wasmmod_registry_set_bench_clock; benches
 * report ns/op and are informational (never a pass/fail gate). */
void pm_wasmmod_registry_set_bench_clock(pm_wasmmod_registry_bench_clock_t clock_us);
int32_t pm_wasmmod_registry_bench_register(const uint8_t *fqn, uint32_t fqn_len, const uint8_t *name,
    uint32_t name_len, pm_wasmmod_registry_bench_fn_t fn);
int32_t pm_wasmmod_registry_bench_register_wasm(const uint8_t *fqn, uint32_t fqn_len,
    const uint8_t *name, uint32_t name_len, const uint8_t *export_name, uint32_t export_len);
uint32_t pm_wasmmod_registry_bench_count(const uint8_t *fqn, uint32_t fqn_len);
int32_t pm_wasmmod_registry_bench_at(const uint8_t *fqn, uint32_t fqn_len, uint32_t index, uint8_t *buf,
    uint32_t *buf_len_io);
int64_t pm_wasmmod_registry_bench_run(const uint8_t *fqn, uint32_t fqn_len, const uint8_t *name,
    uint32_t name_len, uint64_t iterations);
int32_t pm_wasmmod_registry_bench_run_all(const uint8_t *fqn, uint32_t fqn_len, uint64_t iterations,
    uint8_t *buf, uint32_t *buf_len_io);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_REGISTRY_TYPES_H */
