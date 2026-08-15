/*
 * Artifact prepare + wasm/aot registry load — shared by µPy / future CPython.
 * ELF finish (adapters / pack_bind) stays in the Python port.
 */
#ifndef PM_WASMMOD_PORTS_COMMON_LOAD_H
#define PM_WASMMOD_PORTS_COMMON_LOAD_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/wasmmod/pack/format/common/format.h"
#include "pymergetic/wasmmod/registry/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
    uint8_t *owned; /* MICROPY_WASM_FREE when non-NULL */
    mp_wasm_artifact_kind_t kind;
} pm_wasmmod_host_prepared_t;

/* Unwrap optional MPZL, verify sig policy, classify kind. 0 ok, -1 fail. */
int pm_wasmmod_host_prepare(const uint8_t *in, uint32_t in_len, const char *path_hint,
    pm_wasmmod_host_prepared_t *out, char *err, size_t err_len);

/* loader_load for WASM/AOT. Do not use for ELF (port calls elf_publish). */
pm_wasmmod_registry_handle_t pm_wasmmod_host_load_wasm(const char *fqn,
    const uint8_t *bytes, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_COMMON_LOAD_H */
